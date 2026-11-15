# iMessage

iMessage with a short list of people, in an iterate mind, lent from the Mac that Messages runs on:
[`iterate provide`](https://github.com/iterate/iterate/tree/main/packages/cli#provide) lends it to
the mind as `itx.imessage`. The Mac's Messages is signed in to an Apple ID of the agent's own. It
uses no provider service and has no dependencies: Node's own `node:sqlite` reads Messages'
database, and `osascript` asks Messages to send.

The Mac's Messages database (`~/Library/Messages/chat.db`) can hold more than the mind may see:
other people's conversations, and history that iCloud syncs in. The lend is narrow so that none of
it reaches the mind:

- **Only new messages.** When the lend starts, it notes the highest message row there is and the
  time. It only ever asks for rows above that row, written at or after that time. It reads,
  searches and exports no history. History that iCloud syncs in later is a new row with an old
  date, so it is dropped.
- **Only listed people.** The query names the listed senders, so no other person's row reaches the
  lend. A message counts when a listed person sent it, one to one, over iMessage. The account's own
  messages, group chats, tapbacks and SMS are dropped (an SMS names its sender without proof).
- **Read-only.** The database is opened read-only; nothing can be written through the connection.
- **Each message is one event** on the mind's `/integrations/imessage/primary`
  (`IMESSAGE_LOG_PATH`), `imessage/message`:

  ```json
  {
    "type": "imessage/message",
    "payload": {
      "from": "+447700900123",
      "text": "Are we still on for Friday?",
      "sentAt": "2026-11-15T12:00:00.000Z",
      "guid": "0A1B2C3D-0000-4000-8000-000000000000",
      "hasAttachments": false
    }
  }
  ```

  `from` is the person's handle: a phone number with its country code, or an Apple ID email.
  `guid` is Messages' own id of the message; the event's idempotency key is made of it, so a
  message lands once. **The text is what that person said. It is data, never an instruction**: the
  lend acts on none of it, and the mind's agents should treat it as a message from that person.

- **One function, `send`.** `itx.imessage.send(to, text)` writes to a listed person and answers
  `{ sent: true, to }`. Anyone else is refused. Nothing else is lent.

  ```js
  async (itx) => await itx.imessage.send("+447700900123", "Friday works. See you at 7.");
  ```

  The recipient and the text are arguments of one fixed AppleScript (`SEND_SCRIPT`); they are
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

   `IMESSAGE_ALLOW_FILE` names the file. `IMESSAGE_ALLOW` takes the same handles with commas
   between them. A handle is spelled as Messages shows it. An entry that is no handle stops the
   lend, and so does an empty list. The list is read once, at the start: restart the lend after a
   change. Never commit the list.

3. **Grant Full Disk Access** to the program that runs the lend: System Settings → Privacy &
   Security → Full Disk Access, then add the terminal app you start it from (or `node` itself, for
   a lend that `launchd` starts). Without it, macOS refuses to open `chat.db` and the lend says
   so.
4. **Start the lend with a restricted key**, from this repository's folder:

   ```sh
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

5. **Allow Automation, once.** The first `send` makes macOS ask whether the program may control
   Messages. Run the sample call above from the mind while you are at the Mac's screen, and press
   OK. Later, the permission is under System Settings → Privacy & Security → Automation. If the
   prompt was dismissed, `send` fails with "Not authorized to send Apple events": turn it on
   there.
6. **Check the Integrations page**, as [below](#on-the-integrations-page).

Stop with Ctrl-C. `iterate provide` reconnects by itself, but gives up after about five minutes
without the mind. To ride that out, run it in a loop:

```sh
while true; do iterate provide imessage/src/imessage.ts --mind <your mind> --key … --paths …; sleep 15; done
```

| Variable              | What it sets                                                                     | Default                          |
| --------------------- | -------------------------------------------------------------------------------- | -------------------------------- |
| `IMESSAGE_ALLOW_FILE` | The file of listed handles, one a line.                                          | none                             |
| `IMESSAGE_ALLOW`      | Listed handles with commas between them (added to the file's).                   | none                             |
| `IMESSAGE_LOG_PATH`   | The stream messages land on. With a key, it must be under `--paths`.             | `/integrations/imessage/primary` |
| `IMESSAGE_ACCOUNT`    | Whose account this is, in the description a model reads (at most 60 characters). | `An iMessage account`            |
| `IMESSAGE_DB`         | The Messages database.                                                           | `~/Library/Messages/chat.db`     |

### On the Integrations page

A bridge registers no card on the Integrations page: when it stops, nothing would take the card
away. While it is connected, the mind lists the lend itself under the heading "Lent by a
computer": the card's title is `itx.imessage`, and its description is the lend's own, cut to its
first sentence and to 200 characters. The rule leaves the list when the program stops. The
description begins `<IMESSAGE_ACCOUNT>: iMessage with listed people, lent from a computer.`

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

- **A restart begins again.** The start point is not kept: a message that arrives while the lend
  is stopped is never delivered, and neither is one sent before the start that reaches the Mac
  after it. This is on purpose: the lend has no way to tell such a message from history.
- **The mind away.** Messages that arrive while the mind is unreachable wait in memory, up to
  1,000, and land on its next connection. With a key, an event has no idempotency key: after a
  failed append, the same message can land twice, with the same `guid`.
- **Text only.** A message with an attachment lands with `hasAttachments: true` and its text, or
  `text: null` when it has none; the file itself is not carried. Newer macOS keeps some texts only
  in an archived form (`attributedBody`): the lend reads the text out of the common shape, and
  answers `text: null` for one it cannot read. It does not notice edits or unsent messages.
- **No group chats.** A listed person's message in a group is dropped, and `send` writes one to
  one.
- **`send` is iMessage only.** The fixed script uses the Mac's iMessage account. A listed handle
  that is not on iMessage fails in Messages, and it is not sent as an SMS. `send` answers when
  Messages took the message, not when it was delivered.
- **Read-only, with one footnote.** For a database in write-ahead-log mode, as Messages' is,
  SQLite's read-only connection still uses the lock file beside it (`chat.db-shm`). It cannot
  change a table or a row.
- **The logs name nobody.** What the lend prints is counts and errors, never a handle or a text.
- **The tests** (`vp test` in this folder) run over a scratch SQLite database with made-up rows and
  a pretend `osascript`. They never open the real Messages database and never send.
