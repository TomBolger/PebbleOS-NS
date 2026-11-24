# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0
"""Notifications, as the Android companion would push them."""

import math
import struct
import time
import uuid

from pbl.feeds import BlobDb, Feed
from pbl.feeds.music import (
    FLAG_FIRST,
    FLAG_LAST,
    FLAG_NO_IMAGE,
    FLAG_UNSUPPORTED,
    FORMAT_4BIT_PALETTE,
    IMAGING_REQUEST,
    IMAGING_RESPONSE,
    _gcolor8,
    _imaging_packet,
)

ANDROID_DATA_SOURCE = uuid.UUID("ed429c16-f674-4220-95da-454f303f15e2")

# AttributeId
TITLE, BODY, SENDER, APP_NAME = 1, 3, 12, 30
IMAGE_ASPECT_RATIO, NOTIFICATION_SECTION, STACK_ID = 52, 54, 55

# Imaging endpoint: the watch asks for a notification's picture by item id.
IMAGE_TYPE_NOTIFICATION = 1
# Pixel bytes per response chunk, as the phone sends them.
IMAGE_CHUNK = 1000
# Height/width in sixteenths the phone clamps the aspect to.
MIN_ASPECT, MAX_ASPECT = 4, 24

# NotificationSection: section in bits 0-1, category in bits 2-4
SECTIONS = {
    "alerting": 0,
    "conversation": 1,
    "silent": 2,
    "suggested": 2 | 1 << 2,
    "promotions": 2 | 2 << 2,
    "news": 2 | 3 << 2,
    "social": 2 | 4 << 2,
}
TYPE_NOTIFICATION = 1
LAYOUT_NOTIFICATION = 4
FLAG_VISIBLE = 1 << 0
ACTION_DISMISS = 0x04
ACTION_RESPONSE = 0x03

# A day of messages, laid out as the phone's shade lists them: repeated
# senders and "Conversation: Sender" titles exercise the grouped history, and
# each app's notifications form one phone stack.
MESSAGES = [
    {
        "app": "WhatsApp",
        "sender": "Anna",
        "body": "Are we still on for lunch?",
        "age": 26 * 60,
        "section": "conversation",
        "stack": "whatsapp",
    },
    {
        "app": "WhatsApp",
        "sender": "Bob",
        "body": "Sent the slides",
        "photo": True,
        "age": 3 * 60,
        "section": "conversation",
        "stack": "whatsapp",
    },
    {
        "app": "WhatsApp",
        "sender": "Anna",
        "body": "Running 10 min late",
        "age": 40,
        "section": "conversation",
        "stack": "whatsapp",
    },
    {
        "app": "Telegram",
        "title": "PG | Elite: Aloha",
        "body": "Hi team",
        "age": 30,
        "section": "conversation",
        "stack": "telegram",
    },
    {
        "app": "Shop",
        "title": "Weekend sale",
        "body": "20% off everything until Sunday",
        "age": 25,
        "section": "promotions",
    },
    {
        "app": "WhatsApp",
        "sender": "Bob",
        "body": "Check the build please",
        "age": 20,
        "section": "conversation",
        "stack": "whatsapp",
    },
    {
        "app": "Android System",
        "title": "Battery low",
        "body": "15% remaining",
        "age": 10,
        "section": "alerting",
    },
    {
        "app": "News",
        "title": "Morning briefing",
        "body": "Five stories to start your day",
        "age": 8,
        "section": "news",
    },
    {
        "app": "Telegram",
        "title": "PG | Elite: Yevhen",
        "body": "Standup moved",
        "age": 5,
        "section": "conversation",
        "stack": "telegram",
    },
    {
        "app": "WhatsApp",
        "sender": "Anna",
        "body": "Ok see you there",
        "photo": True,
        "age": 1,
        "section": "conversation",
        "stack": "whatsapp",
    },
]


def _attribute(attr_id, text):
    data = text.encode()
    return struct.pack("<BH", attr_id, len(data)) + data


def _stack_id(key):
    """The phone's stack id: Java's String.hashCode(), never 0."""
    h = 0
    for c in key:
        h = (31 * h + ord(c)) & 0xFFFFFFFF
    return h or 1


def notification(notification_id, timestamp, message):
    """A serialized notification (SerializedTimelineItemHeader, attributes,
    Dismiss and Reply actions), from the Android notifications data source."""
    sender = message.get("sender")
    attributes = [
        _attribute(TITLE, message.get("title") or sender),
        _attribute(BODY, message["body"]),
        _attribute(APP_NAME, message["app"]),
    ]
    if sender:
        attributes.insert(0, _attribute(SENDER, sender))
    if message.get("section"):
        attributes.append(
            struct.pack("<BHB", NOTIFICATION_SECTION, 1, SECTIONS[message["section"]])
        )
    if message.get("stack"):
        attributes.append(struct.pack("<BHI", STACK_ID, 4, _stack_id(message["stack"])))
    if message.get("aspect"):
        attributes.append(struct.pack("<BHB", IMAGE_ASPECT_RATIO, 1, message["aspect"]))
    dismiss = struct.pack("<BBB", 0, ACTION_DISMISS, 1) + _attribute(TITLE, "Dismiss")
    reply = struct.pack("<BBB", 1, ACTION_RESPONSE, 1) + _attribute(TITLE, "Reply")
    payload = b"".join(attributes) + dismiss + reply
    header = struct.pack(
        "<16s16sIHBBBBHBB",
        notification_id.bytes,
        ANDROID_DATA_SOURCE.bytes,
        timestamp,
        0,
        TYPE_NOTIFICATION,
        FLAG_VISIBLE,
        0,
        LAYOUT_NOTIFICATION,
        len(payload),
        len(attributes),
        2,
    )
    return header + payload


def aspect_sixteenths(width, height):
    """The ImageAspectRatio attribute: height/width in sixteenths, clamped as
    the phone does."""
    return max(MIN_ASPECT, min(MAX_ASPECT, (height * 16 + width // 2) // width))


class Picture:
    """A picture attached to notifications: a file, or a generated photo of
    hills under a sunset when there is none."""

    SIZE = (400, 300)

    def __init__(self, path=None):
        self._image = None
        if path:
            try:
                from PIL import Image
            except ImportError as e:
                raise ValueError(f"--image FILE needs Pillow ({e})") from e
            self._image = Image.open(path).convert("RGB")
            self.size = self._image.size
        else:
            self.size = self.SIZE

    def pixels(self, width, height):
        """Rows of (r, g, b), centre-cropped and scaled to width x height."""
        src_w, src_h = self.size
        if src_w * height > src_h * width:
            crop_w, crop_h = src_h * width / height, src_h
        else:
            crop_w, crop_h = src_w, src_w * height / width
        x0, y0 = (src_w - crop_w) / 2, (src_h - crop_h) / 2
        if self._image is not None:
            box = (round(x0), round(y0), round(x0 + crop_w), round(y0 + crop_h))
            image = self._image.resize((width, height), box=box)
            data = list(image.getdata())
            return [data[y * width : (y + 1) * width] for y in range(height)]
        return [
            [
                _photo(
                    x0 + (x + 0.5) * crop_w / width, y0 + (y + 0.5) * crop_h / height
                )
                for x in range(width)
            ]
            for y in range(height)
        ]


def _photo(x, y):
    """The generated photo at (x, y) of its 400x300: a sunset over two hills."""
    w, h = Picture.SIZE
    u, v = x / w, y / h
    far = 0.62 + 0.06 * math.sin(u * 7 + 1)
    near = 0.75 + 0.08 * math.sin(u * 4 + 3)
    if v > near:
        shade = (v - near) * 2
        return (int(40 + 60 * shade), int(110 + 40 * shade), 40)
    if v > far:
        return (70, 90, 130)
    sun = math.hypot(u - 0.68, v - 0.45)
    if sun < 0.09:
        return (255, 230, 120)
    return (int(255 - 120 * (1 - v)), int(170 * v + 30), int(200 - 110 * v))


def _nearest(colors, r, g, b):
    return min(
        range(len(colors)),
        key=lambda i: (
            (colors[i][0] - r) ** 2 + (colors[i][1] - g) ** 2 + (colors[i][2] - b) ** 2
        ),
    )


def encode_4bit(rows, width, height):
    """(palette, pixels) as the phone sends a picture: the 16 most used
    screen colors, dithered, two indices per byte with even x high."""
    levels = (0, 85, 170, 255)
    counts = {}
    for row in rows:
        for r, g, b in row:
            key = (round(r / 85), round(g / 85), round(b / 85))
            counts[key] = counts.get(key, 0) + 1
    codes = sorted(counts, key=counts.get, reverse=True)[:16]
    colors = [tuple(levels[c] for c in code) for code in codes]
    error = [[0.0, 0.0, 0.0] for _ in range(width + 2)]
    stride = (width + 1) // 2
    pixels = bytearray(stride * height)
    for y, row in enumerate(rows):
        below = [[0.0, 0.0, 0.0] for _ in range(width + 2)]
        for x, rgb in enumerate(row):
            want = [min(255, max(0, c + e)) for c, e in zip(rgb, error[x + 1])]
            index = _nearest(colors, *want)
            pixels[y * stride + x // 2] |= index << (4 if x % 2 == 0 else 0)
            for i, residual in enumerate(w - c for w, c in zip(want, colors[index])):
                error[x + 2][i] += residual * 7 / 16
                below[x][i] += residual * 3 / 16
                below[x + 1][i] += residual * 5 / 16
                below[x + 2][i] += residual * 1 / 16
        error = below
    return [_gcolor8(*code) for code in codes], bytes(pixels)


class Notifications(Feed):
    name = "notifications"
    help = "A day of messages from a few senders"
    description = (
        "Push a day of notifications from a few senders into the history, "
        "or a single message of your own."
    )

    def add_arguments(self, parser):
        parser.add_argument("--sender", help="Who the message is from")
        parser.add_argument(
            "--body", help="The message text (with --sender or --title)"
        )
        parser.add_argument("--title", help="The title (default: the sender)")
        parser.add_argument(
            "--app", default="WhatsApp", help="The sending app (default: WhatsApp)"
        )
        parser.add_argument(
            "--age",
            type=int,
            default=0,
            metavar="MINUTES",
            help="How long ago it arrived (default: now)",
        )
        parser.add_argument(
            "--section",
            choices=sorted(SECTIONS),
            help="Where the phone's shade lists it",
        )
        parser.add_argument(
            "--stack", metavar="KEY", help="Phone stack it belongs to, e.g. the app"
        )
        parser.add_argument(
            "--image",
            nargs="?",
            const="",
            metavar="FILE",
            help="Attach a picture, as the phone does for a photo in a message: "
            "FILE (needs Pillow), or a generated photo. With the day of messages, "
            "its photos get it. Keeps serving it to the watch until interrupted",
        )
        parser.add_argument(
            "--clear", action="store_true", help="Remove every notification instead"
        )

    def run(self, args, watch, inf):
        if args.clear:
            watch.blobdb_clear(BlobDb.NOTIFS)
            inf("cleared every notification")
            return

        if args.sender or args.title or args.body:
            if not args.body or not (args.sender or args.title):
                raise ValueError("a message needs --body and --sender or --title")
            messages = [
                {
                    "app": args.app,
                    "sender": args.sender,
                    "title": args.title,
                    "body": args.body,
                    "age": args.age,
                    "section": args.section,
                    "stack": args.stack,
                    "photo": True,
                }
            ]
        else:
            messages = MESSAGES

        picture = Picture(args.image) if args.image is not None else None
        # The watch asks for a picture as soon as its card shows: answer from the start.
        pictured = set()
        if picture and not watch.dry_run:
            self._serve_pictures(watch, inf, picture, pictured)
        now = int(time.time())
        for message in messages:
            notification_id = uuid.uuid4()
            if picture and message.get("photo"):
                message = dict(message, aspect=aspect_sixteenths(*picture.size))
                pictured.add(notification_id.bytes)
            watch.blobdb_insert(
                BlobDb.NOTIFS,
                notification_id,
                notification(notification_id, now - message["age"] * 60, message),
            )
        count = len(messages)
        inf(f"pushed {count} notification{'s' if count != 1 else ''}")
        if pictured and not watch.dry_run:
            inf("serving the picture; Ctrl-C to stop")
            try:
                while True:
                    time.sleep(1)
            except KeyboardInterrupt:
                inf("stopped")

    @staticmethod
    def _serve_pictures(watch, inf, picture, pictured):
        """Answer the watch's requests for the notifications in ``pictured``,
        as the phone does."""

        def reply(token, flags, offset=0, chunk=b"", header=b""):
            packet = _imaging_packet()(
                cmd=IMAGING_RESPONSE,
                token=token,
                payload=struct.pack("<BIH", flags, offset, len(chunk)) + header + chunk,
            )
            watch.send(packet, "Imaging response")

        def on_imaging(packet):
            if packet.cmd != IMAGING_REQUEST or len(packet.payload) < 6:
                return
            image_type, fmt, width, height = struct.unpack_from("<BBHH", packet.payload)
            flags = image_type << 4
            if image_type != IMAGE_TYPE_NOTIFICATION:
                reply(packet.token, flags | FLAG_UNSUPPORTED)
                return
            item_id = bytes(packet.payload[6:22])
            if item_id not in pictured or fmt != FORMAT_4BIT_PALETTE:
                reply(packet.token, flags | FLAG_NO_IMAGE)
                return
            inf(f"watch: picture for {uuid.UUID(bytes=item_id)}, {width}x{height}")
            palette, pixels = encode_4bit(picture.pixels(width, height), width, height)
            header = struct.pack("<HHBB", width, height, fmt, len(palette)) + bytes(
                palette
            )
            for offset in range(0, len(pixels), IMAGE_CHUNK):
                chunk = pixels[offset : offset + IMAGE_CHUNK]
                chunk_flags = flags | (FLAG_FIRST if offset == 0 else 0)
                if offset + len(chunk) >= len(pixels):
                    chunk_flags |= FLAG_LAST
                reply(
                    packet.token,
                    chunk_flags,
                    offset,
                    chunk,
                    header if offset == 0 else b"",
                )

        watch.on(_imaging_packet(), on_imaging)
