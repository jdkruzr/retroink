---
title: Obsidian Clipping Sync
nav_order: 7.6
---

# Obsidian Clipping Sync

Obsidian Clipping Sync pushes the highlights you save while reading (the same
ones written to `/My Clippings.txt`) into an Obsidian vault, without a
computer in between.

There is no public Obsidian Sync protocol to build against, so this feature
talks to one of two things instead:

- **[Local REST API with MCP](https://github.com/coddingtonbear/obsidian-local-rest-api)
  plugin** (recommended), built by Adam Coddington. It runs an HTTPS API on
  your phone or computer, inside your vault. RetroInk posts each new
  highlight straight into a note in your vault over your local Wi-Fi
  network. No cloud account, no internet dependency: whatever syncs your
  vault already (Obsidian Sync, iCloud, Syncthing, Git) carries it from
  there.
- **Generic webhook**. RetroInk POSTs a JSON payload to any URL you give it.
  Point this at n8n, Make, Home Assistant, or a small script of your own if
  you want clippings routed somewhere the Local REST API plugin can't reach
  (e.g. over the internet, or into a different note-taking app).

## How it works

Every highlight you save in the reader is queued locally (a small file on the
SD card) as soon as it's saved to `/My Clippings.txt`. No network activity,
no delay in the reader. Nothing is sent until a sync runs. Sync is:

- **Manual**, any time, from the web UI's Settings page ("Sync Now"), or
- **Automatic when File Transfer / Calibre Wireless mode starts**, if you
  turn that on. Wi-Fi is already up then, so this is the only way to get
  clippings synced without spending extra battery keeping Wi-Fi on while you
  read.

A sync stops at the first clipping it can't deliver, so nothing already sent
is lost and nothing unsent is skipped. Whatever didn't go through stays
queued for the next attempt, unless it's now failed 5 times in a row: at
that point it's almost certainly a permanent rejection rather than a
transient hiccup (a malformed clipping, a rule on the receiving end), so
RetroInk gives up on that one clipping specifically, logs it to
`.crosspoint/obsidian-failed.jsonl` on the SD card, and keeps going instead
of letting it block everything behind it forever.

## Setup: Local REST API plugin

1. In Obsidian, install and enable **Local REST API with MCP** (by Adam
   Coddington), from the community plugins list.
2. Open its settings and copy the **API key** and the **HTTPS URL** it shows
   (e.g. `https://192.168.1.50:27124`). The plugin's certificate is
   self-signed, which is why RetroInk defaults to skipping certificate
   verification for this target. You're relying on your Wi-Fi network being
   trusted, not on the certificate.
3. Make sure the device running Obsidian and your RetroInk reader are on the
   **same Wi-Fi network**.
4. On the reader's web Settings page (**File Transfer > Join Network** or
   **Create Hotspot**, then open the shown URL and go to **Settings**), find
   the **Obsidian Clipping Sync** card:
   - Target: **Obsidian Local REST API plugin**
   - Base URL: the HTTPS URL from the plugin. Prefer the host's `.local`
     hostname (e.g. `https://your-mac.local:27124`) over its raw IP: an IP
     assigned by DHCP can change when the host reboots or reconnects to
     Wi-Fi, silently breaking sync until you notice and update it.
   - Fallback URL (optional): tried only if the Base URL can't be reached at
     all. Pairs well with a raw IP here as a backup for a `.local` hostname
     above, or vice versa.
   - API key: the token from the plugin
   - Note path: where clippings land, e.g. `Clippings/{book}.md`. `{book}`
     is replaced with the book's title. Point this at a folder you don't
     otherwise hand-edit; each sync **appends** to the note, and RetroInk
     falls back to creating it with a fresh write the first time a note
     doesn't exist yet.
   - Enabled: on
5. Save, then highlight something and hit **Sync Now**.

## Setup: generic webhook

1. Set Target to **Generic webhook** and Base URL to your endpoint.
2. Optionally set an API key. It's sent as `Authorization: Bearer <key>` if
   present, otherwise omitted.
3. Each clipping arrives as its own POST:

   ```json
   {
     "source": "RetroInk",
     "book": "Book Title",
     "author": "Author Name",
     "chapter": "Chapter 3",
     "page": 42,
     "text": "The highlighted passage.",
     "timestamp": 1732900000
   }
   ```

   Wire that into whatever writes it into your vault: an n8n/Make flow
   calling the Local REST API plugin itself, a Home Assistant automation, or
   your own script.

## Notes and limits

- **Nothing is sent while you're just reading.** Saving a highlight only
  writes to the SD card (same as `/My Clippings.txt` always has). Network
  activity only happens during a sync.
- **The Local REST API target only works on the same local network** as
  whatever's running Obsidian. For sync over the internet, use the webhook
  target and route it yourself.
- **Verify the plugin's POST/PUT behavior for your installed version.** This
  integration assumes `POST /vault/<path>` appends to an existing note and
  that a `404` means the note doesn't exist yet (handled by falling back to
  `PUT`, which creates/overwrites). Check the plugin's own docs if syncing
  behaves unexpectedly.
- Pending clippings are capped at roughly 512 KB on the SD card (many
  thousands of highlights) so a forgotten sync can't grow without bound;
  syncing regularly avoids ever approaching that.
- This feature does not touch or replace `/My Clippings.txt`. That
  Kindle-format export keeps working exactly as before, independent of
  whether Obsidian sync is enabled.

## Security notes

- **"Skip TLS verification" trusts whatever certificate the server presents.**
  This is necessary for the Local REST API plugin's self-signed certificate,
  but it means the reader can't tell your real Obsidian instance apart from
  another device on the same Wi-Fi network impersonating it. This is bounded
  to "someone is already on your trusted network," the same assumption this
  firmware's whole File Transfer web server already makes (it has no
  authentication either). Avoid running File Transfer with Obsidian sync
  configured on untrusted or public Wi-Fi.
- **The reader refuses to sync if a target URL is `http://` (not `https://`)
  while an API key is set**, rather than sending that key in cleartext. If
  you hit this, switch the URL to `https://` or remove the key.
- **The API key is obfuscated on the SD card, not encrypted**, XORed against
  this device's hardware MAC address and base64-encoded, the same scheme
  every other saved password in this firmware uses (Wi-Fi, OPDS, KOReader
  sync). It stops casual reading of the SD card, not a targeted attacker who
  also has the device.

### Heads up: the plugin is reachable by your whole Wi-Fi network, not just the reader

Here's the tradeoff hiding under the hood. The Local REST API plugin has no
way of knowing ahead of time which device on your network is your RetroInk
reader, so it has to bind to `0.0.0.0`, "accept connections from anywhere,"
just to let the reader reach it at all. The side effect is that every other
device on the same Wi-Fi can technically reach that port too. The only thing
standing between "on the network" and "full read/write access to your
vault" is the API key, and that key isn't scoped to just appending
clippings; it's the same access Obsidian itself has.

So who could actually pull this off? Someone already on your Wi-Fi network
who also has your API key, whether they guessed it, intercepted it, or
found it lying around somewhere. It's not something a stranger on the
internet could reach unless your router is forwarding that port in from the
outside world, and nothing in this setup asks you to do that. Please don't
set that up.

If someone did get in with a valid key, there's currently no way in the
plugin to limit them to a single folder or to read-only access, so they'd
be able to read, write, and delete anywhere in the vault. Worth checking the
plugin's own settings every so often in case that changes down the line.

For what it's worth, this isn't a RetroInk-specific problem. It's the same
trust boundary the whole File Transfer web server on this firmware already
runs on (no login there either), just stretched to cover whatever's
listening on your Obsidian machine too.

**A few ways to close the gap, roughly easiest first:**

1. **Treat the API key like a password.** Don't paste it anywhere else,
   don't commit it to a repo, and regenerate it from the plugin's settings
   the moment you suspect it leaked. Do this one no matter what else you
   pick from the list below.
2. **Only turn on "Auto-sync on File Transfer" if you'll actually use it.**
   The plugin only needs to be reachable while a sync is happening. Leaving
   it on all the time just widens the window for no real benefit if you're
   syncing manually anyway.
3. **Lock the plugin's port to your reader's IP with a firewall rule on the
   machine running Obsidian.** This is the one that actually closes the gap
   without breaking the reader's own access, so it's worth the extra effort
   if you can spare it. On macOS, the built-in Application Firewall only
   works by app, not by port and source IP, so it can't do this; `pf`
   (packet filter) can. Here's a minimal example that only lets one IP
   through and blocks everyone else:

   ```
   # /etc/pf.anchors/obsidian-rest-api, replace 192.168.1.50 with your reader's IP
   block in proto tcp from any to any port 27124
   pass in proto tcp from 192.168.1.50 to any port 27124
   ```

   Reference that anchor from `/etc/pf.conf` and load it with
   `sudo pfctl -f /etc/pf.conf -e`. `pf` rules don't survive a reboot on
   their own, you'd need a LaunchDaemon for that, so treat this as a
   weekend project rather than a five-minute fix, and confirm it took with
   `pfctl -s rules` before trusting it.
4. **Or do the same thing at your router instead of the host machine**, if
   your router supports custom firewall rules or ACLs (common on prosumer
   gear like UniFi, pfSense, OPNsense, or ASUS running Merlin firmware).
   Look for a rule that blocks the port from the LAN except from one
   allowed IP. One thing to avoid here: your router's blanket "client
   isolation" or "AP isolation" toggle looks like it would help, but it
   isolates every device from every other device on the network, including
   the reader itself, so it would just break the feature. You want an
   allowlist scoped to one device, not a blanket wall.
5. **And if none of that feels worth doing, that's a fair call too.** The
   risk you're accepting comes down to "someone with hostile intent is
   already on my home Wi-Fi," which is the same baseline every other
   login-free feature on this firmware already runs on. Decide if that's
   good enough for your situation the same way you would for any other
   device on your network that has no login screen.

## Troubleshooting

**"Sync target rejected the API key"**

Recheck the API key in Settings. It's stored obfuscated and never round-trips
back to the browser, so a typo requires re-entering it, not just re-saving.

**"Could not reach the sync target"**

Confirm the reader and the Local REST API plugin's device are on the same
Wi-Fi network, and that the Base URL (including port) matches what the plugin
shows. For a webhook target, confirm the endpoint is reachable from your
Wi-Fi network (or the internet, if that's where it lives).

**Clippings pile up in "pending" and never seem to sync**

Auto-sync only fires when File Transfer or Calibre Wireless mode starts, not
continuously in the background. Use **Sync Now** in the web UI at any time to
drain the queue immediately.

**"Sync Now" says some clippings were skipped**

One or more clippings failed to deliver 5 times in a row and were given up
on rather than left blocking the rest of the queue forever. Check
`.crosspoint/obsidian-failed.jsonl` on the SD card (plain JSON, one clipping
per line, with a `reason` field) to see what didn't make it and why.
