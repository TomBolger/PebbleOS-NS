# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0
"""Notifications, as the Android companion would push them."""

import struct
import time
import uuid

from pbl.feeds import BlobDb, Feed

ANDROID_DATA_SOURCE = uuid.UUID("ed429c16-f674-4220-95da-454f303f15e2")

# AttributeId
TITLE, BODY, SENDER, APP_NAME = 1, 3, 12, 30
NOTIFICATION_SECTION, STACK_ID = 54, 55

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
                }
            ]
        else:
            messages = MESSAGES

        now = int(time.time())
        for message in messages:
            notification_id = uuid.uuid4()
            watch.blobdb_insert(
                BlobDb.NOTIFS,
                notification_id,
                notification(notification_id, now - message["age"] * 60, message),
            )
        count = len(messages)
        inf(f"pushed {count} notification{'s' if count != 1 else ''}")
