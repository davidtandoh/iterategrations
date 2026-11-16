# iMessage

iMessage with a short list of people and group chats, in an iterate mind, lent from the Mac that Messages runs on:
[`iterate provide`](https://github.com/iterate/iterate/tree/main/packages/cli#provide) lends it to
the mind as `itx.imessage`. The Mac's Messages is signed in to an Apple ID of the agent's own. It
uses no provider service and has no dependencies: Node's own `node:sqlite` reads Messages'
database, and `osascript` asks Messages to send.

The Mac's Messages database (`~/Library/Messages/chat.db`) can hold more than the mind may see:
other people's conversations, and history that iCloud syncs in. The lend is narrow so that none of
it reaches the mind:

- **Only new messages.** On its first start, the lend notes the highest message row there is and the
  time. It only ever asks for rows above that row, written at or after that time. It reads,
  searches and exports no history. History that iCloud syncs in later is a new row with an old
  date, so it is dropped. For restart behavior, see [Keep it running](#keep-it-running).
- **One-to-one messages only from listed people.** The query names the listed senders for
  one-to-one messages. A message counts when a listed person sent it over iMessage. The account's own
  messages, tapbacks and SMS are dropped (an SMS names its sender without proof).
- **Group chats only when listed.** A group chat is dropped unless the allow file lists it, on a
  line of its own form (`group:<chat identifier>`). A message in a listed group counts whoever in
  the group wrote it, and lands with the group's id and the sender's handle. Every other guard
  still applies to it: new, over iMessage, written to the agent's own address.
- **Only the agent's own account.** Another account's messages can sync into the same Messages
  store, such as the owner's personal one through iCloud. So the lend must be told its own
  address (`IMESSAGE_ACCOUNT`), and a message counts only when it was written to that address
  (`destination_caller_id` in the database) and, where the row names the receiving account
  (`account`), received by it. A new message from a listed person to another account on the same
  Mac is dropped. Without `IMESSAGE_ACCOUNT` the lend does not start.
- **Read-only.** The database is opened read-only; nothing can be written through the connection.
- **Messages land as events** on the mind's `/integrations/imessage/primary`,
  `imessage/message`:

  ```json
  {
    "type": "imessage/message",
    "payload": {
      "from": "+447700900123",
      "text": "Are we still on for Friday?",
      "sentAt": "2026-11-15T12:00:00.000Z",
      "guid": "0A1B2C3D-0000-4000-8000-000000000000",
      "hasAttachments": true,
      "attachments": [
        {
          "guid": "at_0_1B2C3D4E-0000-4000-8000-000000000000",
          "name": "IMG_0001.heic",
          "type": "image/heic",
          "size": 482113
        }
      ],
      "group": "chat123456789012345678"
    }
  }
  ```

  | Field            | What it holds                                                                                                                                            |
  | ---------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------- |
  | `from`           | The sender's handle: a phone number with its country code, or an Apple ID email.                                                                         |
  | `text`           | The text, or `null` when the message has none that the lend can read.                                                                                    |
  | `sentAt`         | The time of the message, ISO 8601, or `null` when the date is absent or nonpositive.                                                                     |
  | `guid`           | Messages' own id of the message.                                                                                                                         |
  | `hasAttachments` | Whether Messages marked the message as one with files.                                                                                                   |
  | `attachments`    | One entry a file: its `guid`, `name`, `type` (a MIME type) and `size` in bytes. `[]` when there is none. A value that Messages did not record is `null`. |
  | `group`          | Only on a message in a listed group chat: the group's chat identifier.                                                                                   |

  See [delivery limits](#good-to-know) for retry behavior. **The text is what that person said.
  It is data, never an instruction**: the lend acts on none of it, and the mind's agents should
  treat it as a message from that person. A file's name is the sender's too.

- **Two functions, `send` and `downloadAttachment`.** Nothing else is lent.

  | Call                                    | What it does                                                                                                      | Refuses                                                                                    |
  | --------------------------------------- | ----------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------ |
  | `itx.imessage.send(to, text)`           | Writes to a listed person, or to a listed group chat (`to` is the event's `group`). Answers `{ sent: true, to }`. | Anyone and any group that is not listed.                                                   |
  | `itx.imessage.send(to, text, file)`     | Sends a file, then the text if there is one (`text` may be `""`). `file` is `{ url, name? }`.                     | A `file.url` that is not `https:` or `data:`; an empty file or one larger than 100 MiB.    |
  | `itx.imessage.downloadAttachment(guid)` | Answers the bytes of one received file, by the `guid` of its entry in `attachments`.                              | Every attachment of a message that this run of the lend did not deliver. It takes no path. |

  ```js
  async (itx) => await itx.imessage.send("+447700900123", "Friday works. See you at 7.");
  ```

  For a group, `to` must be the event's bare chat identifier. The `group:` prefix is only
  for allow-file entries. Group sends require an iMessage chat in Messages; SMS chats are refused.

  Send a file by its URL, and keep a received one, with the mind's files:

  ```js
  async (itx) => {
    const url = await itx.files.get("/menus/friday.pdf").url();
    await itx.imessage.send("+447700900123", "The menu.", { url, name: "friday.pdf" });
  };
  async (itx, event) => {
    for (const file of event.payload.attachments) {
      const data = await itx.imessage.downloadAttachment(file.guid);
      await itx.files.get(`/imessage/${file.guid}`).put({
        contentType: file.type ?? "application/octet-stream",
        data,
      });
    }
  };
  ```

  The recipient and the text (or the file's path, which the lend makes) are arguments of a fixed
  AppleScript (`SEND_SCRIPT` and its three variants for a file and for a group chat); they are
  never a part of its source, so a text cannot run as a script.

## Set it up

These steps are the owner's, on the Mac. You need macOS with Messages signed in, Node 22.18 or
later, and this repository.

1. **Install the `iterate` CLI and sign in**: `npm install -g iterate@latest`, then
   `iterate login <your mind>` (its address, such as `example.com`).
2. **List the people.** Write a file outside this repository, one handle a line:

   ```sh
   mkdir -p ~/.config/iterate-imessage
   cat > ~/.config/iterate-imessage/allow <<'EOF'
   # one handle a line: a phone number with its country code, or an Apple ID email
   +447700900123
   sam@example.com
   EOF
   chmod 600 ~/.config/iterate-imessage/allow
   ```

   `IMESSAGE_ALLOW_FILE` names the file and is the only source of listed people and group chats.
   A handle is spelled as Messages shows it. An entry that is neither a handle nor a group line
   stops the lend, and so does an empty list. The list is read once, at the start: restart the lend after a
   change. Never commit the list.

   To list a group chat, add a line `group:<chat identifier>`. The chat identifier is the name
   Messages keeps for the group in its database: `chat` and a long number. The agent's account
   must already be in the group. Look the identifier up on the Mac, from a terminal that has
   Full Disk Access (step 4):

   ```sh
   sqlite3 -readonly ~/Library/Messages/chat.db \
     "SELECT chat_identifier, display_name FROM chat WHERE chat_identifier LIKE 'chat%'"
   ```

   ```text
   group:chat123456789012345678   # the Friday dinner group
   ```

   A group line lists the whole group: every member's messages in it reach the mind, also those
   of a member who is not listed as a person. A chat identifier without `group:` is refused.

3. **Name the agent's own address**: `IMESSAGE_ACCOUNT` is the address people write to the agent
   at, as Messages shows it under Settings → iMessage ("You can be reached for messages at"): the
   dedicated Apple ID's email, or its phone number with the country code. An account that is
   reached at several addresses takes them all, with commas between them. Never put the owner's
   personal address here.
4. **Grant Full Disk Access** to the program that runs the lend: System Settings → Privacy &
   Security → Full Disk Access, then add the terminal app you start it from (or `node` itself, for
   a lend that `launchd` starts). Without it, macOS refuses to open `chat.db` and the lend says
   so.
5. **Start the lend with a restricted key**, from this repository's folder:

   ```sh
   IMESSAGE_ACCOUNT=agent@example.com \
   IMESSAGE_ALLOW_FILE=~/.config/iterate-imessage/allow \
     iterate provide imessage/src/imessage.ts --mind <your mind> \
     --key ~/.config/iterate-imessage/key --paths /integrations/imessage/
   ```

   `--key` and `--paths` give the lend a key of its own. The first run prints a link to the mind's
   own page: open it, sign in, and press Allow, once. Later runs connect with no sign-in. The key
   may lend `itx.imessage` and add events under `/integrations/imessage/`, and nothing else in the
   mind. Whoever can read the key file is the lend: keep it at mode 600 and out of any repository.
   A member removes the key on the mind's `/keys` page. Without `--key` and `--paths`, the lend
   uses your own sign-in and acts as you.

6. **Allow Automation, once.** The first `send` makes macOS ask whether the program may control
   Messages. Run the sample call above from the mind while you are at the Mac's screen, and press
   OK. Later, the permission is under System Settings → Privacy & Security → Automation. If the
   prompt was dismissed, `send` fails with "Not authorized to send Apple events": turn it on
   there.
7. **Check the Integrations page**, as [below](#on-the-integrations-page).

Stop with Ctrl-C.

| Variable              | What it sets                                                                         | Default                                                |
| --------------------- | ------------------------------------------------------------------------------------ | ------------------------------------------------------ |
| `IMESSAGE_ACCOUNT`    | Required. The agent's own address or addresses: only messages written to them count. | none: the lend does not start                          |
| `IMESSAGE_ALLOW_FILE` | The file of listed handles and `group:` lines, one a line.                           | none                                                   |
| `IMESSAGE_STATE_FILE` | A file that keeps the start point across restarts ([below](#keep-it-running)).       | none: each start begins from now                       |
| `IMESSAGE_SEND_DIR`   | The folder a file waits in while Messages sends it.                                  | `~/Library/Messages/Attachments/iterate-imessage-send` |
| `IMESSAGE_DB`         | The Messages database.                                                               | `~/Library/Messages/chat.db`                           |

### Keep it running

`iterate provide` reconnects by itself, but gives up after about five minutes without the mind.
Two ways restart it. Use one of them.

| Way                     | Use it when                                     | What restarts the lend                   |
| ----------------------- | ----------------------------------------------- | ---------------------------------------- |
| A loop in a terminal    | You are trying the lend out.                    | The loop, while the terminal stays open. |
| A `launchd` LaunchAgent | The lend must run unattended, as on a Mac mini. | macOS, after each exit and each login.   |

**A loop.** Export the variables in the shell first, then:

```sh
while true; do iterate provide imessage/src/imessage.ts --mind <your mind> --key … --paths …; sleep 15; done
```

**A LaunchAgent.** `launchd` is the macOS service manager. A LaunchAgent is a job that `launchd`
runs for one user while that user is logged in. Messages needs the user's session, so the lend is
a LaunchAgent, not a system daemon. This repository installs nothing: the steps are the owner's.

1. Find the two programs the job runs. `launchd` gives a job no shell profile, so the job names
   each program by its full path:

   ```sh
   realpath "$(which node)"   # the node binary, for example /opt/homebrew/Cellar/node/24.1.0/bin/node
   which iterate              # the iterate CLI, for example /opt/homebrew/bin/iterate
   ```

2. Grant Full Disk Access to that `node` binary: System Settings → Privacy & Security → Full Disk
   Access → `+`, then press Cmd-Shift-G and paste the path from step 1. Under `launchd` no
   terminal app runs the lend, so the terminal's own grant does not count. Grant it again after
   a Node upgrade that changes the path.
3. Write `~/Library/LaunchAgents/com.example.iterate-imessage.plist`. Replace each path, the
   address and the mind with your own. Keep the file out of any repository.

   ```xml
   <?xml version="1.0" encoding="UTF-8"?>
   <!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
   <plist version="1.0">
   <dict>
     <key>Label</key>
     <string>com.example.iterate-imessage</string>
     <key>ProgramArguments</key>
     <array>
       <string>/opt/homebrew/Cellar/node/24.1.0/bin/node</string>
       <string>/opt/homebrew/bin/iterate</string>
       <string>provide</string>
       <string>imessage/src/imessage.ts</string>
       <string>--mind</string>
       <string>example.com</string>
       <string>--key</string>
       <string>/Users/sam/.config/iterate-imessage/key</string>
       <string>--paths</string>
       <string>/integrations/imessage/</string>
     </array>
     <key>WorkingDirectory</key>
     <string>/Users/sam/iterategrations</string>
     <key>EnvironmentVariables</key>
     <dict>
       <key>IMESSAGE_ACCOUNT</key>
       <string>agent@example.com</string>
       <key>IMESSAGE_ALLOW_FILE</key>
       <string>/Users/sam/.config/iterate-imessage/allow</string>
       <key>IMESSAGE_STATE_FILE</key>
       <string>/Users/sam/.config/iterate-imessage/state.json</string>
       <key>PATH</key>
       <string>/opt/homebrew/bin:/usr/bin:/bin</string>
     </dict>
     <key>RunAtLoad</key>
     <true/>
     <key>KeepAlive</key>
     <true/>
     <key>ThrottleInterval</key>
     <integer>15</integer>
     <key>ProcessType</key>
     <string>Background</string>
     <key>StandardOutPath</key>
     <string>/Users/sam/Library/Logs/iterate-imessage.log</string>
     <key>StandardErrorPath</key>
     <string>/Users/sam/Library/Logs/iterate-imessage.log</string>
   </dict>
   </plist>
   ```

   `KeepAlive` makes `launchd` start the program again each time it exits, and `ThrottleInterval`
   makes it wait 15 seconds first: together they are the loop above. `RunAtLoad` starts it at
   login.

4. Run the lend once in a terminal with `--key` (step 5 of the setup) before the first load, so
   that the key is already allowed. The job cannot open the sign-in link.
5. Load the job, and read its output:

   ```sh
   launchctl bootstrap gui/$(id -u) ~/Library/LaunchAgents/com.example.iterate-imessage.plist
   tail -f ~/Library/Logs/iterate-imessage.log
   ```

   `launchctl bootout gui/$(id -u)/com.example.iterate-imessage` stops and unloads it.

6. Allow Automation for the job, once: the first `send` under `launchd` makes macOS ask whether
   `node` may control Messages. Be at the Mac's screen for it (step 6 of the setup).
7. Check the Integrations page, as [below](#on-the-integrations-page).

The log can hold private text ([Logs](#good-to-know)): keep it out of backups you share.

**The start point across restarts.** Without `IMESSAGE_STATE_FILE`, each start begins from now: a
message that arrives while the lend is stopped is never delivered. With it, the lend keeps two
numbers in that file (mode 600): the last message row it is done with, and the time of its very
first start. A restart then goes on after that row.

| Case                                                       | With a state file                                 |
| ---------------------------------------------------------- | ------------------------------------------------- |
| The very first start                                       | Begins from now. Nothing older is ever delivered. |
| A message that arrived while the lend was stopped          | Delivered at the next start.                      |
| A message read but not yet taken by the mind               | Delivered again at the next start.                |
| History that iCloud syncs in, dated before the first start | Dropped: the date floor never moves.              |
| The file is missing or unreadable                          | Begins from now.                                  |
| The file is malformed                                      | Logs a notice and begins from now.                |
| The saved row exceeds the database's highest row           | Begins from now.                                  |

After a long stop, the next start delivers every listed message that arrived meanwhile (the
newest 1,000). To begin from now instead, delete the state file before the start.

### On the Integrations page

The [bridge contract](../adding-an-integration.md#keep-the-integrations-page-true) describes
how the mind lists live lends. This lend appears as `itx.imessage` under "Lent by a computer",
with the description `An iMessage account: iMessage with listed people, lent from a computer.`

Last, before you tell the person it is done, check that it is lent:

```js
async (itx) => (await itx.rewriteRules.list()).find((rule) => rule.match === "itx.imessage");
```

A rule whose target is `itx.builtins.rpcStubs.get('imessage')` is the live lend, with its
`description`. No rule: the bridge is not connected, so read its output.

### Take it away

Stop the program: the rule and the card go with it. Remove the key on the mind's `/keys` page and
delete the key file and the allow-list. The events it appended stay on
`/integrations/imessage/primary`. Then run the check above: it must answer nothing.

## Good to know

- **Restarts** follow [the start-point rules](#keep-it-running). A message sent before the
  first start that reaches the Mac after it is never delivered. This is on purpose: the lend has
  no way to tell such a message from history.
- **The mind away.** Messages that arrive while the mind is unreachable wait in memory, up to
  1,000. When the queue exceeds that limit, the oldest queued messages are dropped and the
  lend logs a count. Retained messages are retried on the next poll or connection. With a
  restricted key, an event has no idempotency key: after a failed append, the same message can
  land twice, with the same `guid`. Without a restricted key, the append uses an idempotency
  key derived from `guid`.
- **Received files.** The event names each file; the bytes stay on the Mac until the mind asks
  with `downloadAttachment(guid)`. The lend answers only for attachments of messages that this
  run of it delivered, retaining the 5,000 most recently discovered attachment GUIDs. After a
  restart the earlier ones are refused. It refuses files larger than 100 MiB. It reads the path
  from the database and requires the real file to be inside
  `~/Library/Messages/Attachments`. A file that Messages has not downloaded to the Mac yet is
  "not found": ask again later. `hasAttachments: true` with `attachments: []` means Messages had
  not written the attachment metadata yet. The text is delivered immediately. For two minutes
  after delivery, this run checks only those delivered messages for missing metadata. If the
  metadata appears, the lend appends one follow-up event with the same message `guid` and its
  attachment metadata, and makes the files downloadable. The follow-up has a separate
  idempotency key when appended without a restricted key, as described under **The mind away**.
  After two minutes, the lend silently stops checking that message. Discovery
  is held in memory for at most 5,000 messages and does not survive a restart.
- **Sent files.** The lend fetches the URL from the Mac, at most 100 MiB, and follows a redirect
  only to another `https:` URL. It writes the bytes to a file (mode 600) in a new folder (mode 700) under `IMESSAGE_SEND_DIR`, hands Messages the path, and deletes the folder five minutes
  later, when Messages has had time to read it. A failed send deletes it at once, and each start
  deletes what an earlier run left. The default folder is inside Messages' own, because Messages
  may not read every folder of the disk. The name is `file.name` or the URL's last part, with
  nothing of a path kept.
- **Text.** Newer macOS keeps some texts only in an archived form (`attributedBody`): the lend
  reads the text out of the common shape, and answers `text: null` for one it cannot read. It
  does not notice edits or unsent messages.
- **Group chats.** A group that is not listed is dropped, also when a listed person writes in it.
  `send` to a listed group needs the group to exist in Messages on the Mac: the lend reads the
  group's id from the database and refuses when there is none. Only iMessage groups count; a
  group with an SMS member is not one.
- **`send` is iMessage only.** The fixed script uses the Mac's iMessage account. A listed handle
  that is not on iMessage fails in Messages, and it is not sent as an SMS. `send` answers when
  Messages took the message, not when it was delivered.
- **Read-only, with one footnote.** For a database in write-ahead-log mode, as Messages' is,
  SQLite's read-only connection still uses the lock file beside it (`chat.db-shm`). It cannot
  change a table or a row.
- **Nothing lands?** When a listed person's new message was for another address, the lend prints
  how many it did not pass on. If that count grows while people write to the agent,
  `IMESSAGE_ACCOUNT` is not the address Messages records: correct it and restart. A Messages
  database with no `destination_caller_id` column is refused at the start.
- **`send` and several accounts.** `send` uses the Mac's first iMessage account. Keep Messages
  signed in to the agent's Apple ID alone, so that this is the agent's.
- **Logs.** Queue and account-filter notices contain counts. Errors include text returned by
  SQLite, the mind or `osascript`; treat that output as potentially private.
- **The tests** (`vp test` in this folder) run over a scratch SQLite database with made-up rows and
  a pretend `osascript`. They never open the real Messages database and never send.
