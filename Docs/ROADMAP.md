# Campus Combat Roadmap

Started 2026-10-15. Items are ticked as they are finished, verified and pushed. **(you)** = needs the owner.

## 1. Verify today's work

- [ ] Full review of all of today's changes (game, backend, admin) for bugs and security issues
- [ ] Tidy-up: split `CampusLobbyUI.cpp` and `CampusBackend.cpp` into smaller files, no behaviour change
- [ ] Small fix: anti-cheat bans also go into the ban history
- [ ] Small fix: tests get their own database, so the random matchmaker failure stops
- [ ] Build the APK and test on the phone, with screenshots (select map, wardrobe, decor, maintenance/update, announcements, terms) **(you: plug in the phone)**
- [ ] Fix what the phone test finds
- [ ] Create the first owner admin account **(you: `npm run admin:create`)**

## 2. Admin phase B

- [ ] Revenue screen: Play Store vs Razorpay, refunds, top items
- [ ] Store and economy editor: prices, sales, bundles
- [ ] Diamond grants and refunds, second owner's approval above a limit
- [ ] Image uploads + game shows remote banners and store art without a new APK
- [ ] Live events with rewards
- [ ] Modes, maps and BR schedule from the admin panel
- [ ] Moderation queue for player reports
- [ ] Push notifications for invites (optional) **(you: decide; create a Firebase project)**

## 3. Admin phase C

- [ ] Analytics: daily active players, retention, matches, crashes
- [ ] Live sessions, rooms and queues, force-end a stuck session
- [ ] Anti-cheat review screen
- [ ] Push campaigns from the admin panel
- [ ] Season and college leaderboard tools

## 4. Moving to AWS

- [ ] Production settings: real S3, matchmaker as its own worker, backups to S3, remove break-glass token, lower-privilege DB user, CloudWatch
- [ ] Linux dedicated-server build of the game for match servers
- [ ] Test deploy on EC2, point the game at the domain **(you: AWS account, domain, region)**

## 5. Real art

- [ ] Outfits: Blender script to fit downloaded characters to the game skeleton **(you: download 3-5 characters)**
- [ ] Decorations: free Fab props, credited in `CREDITS.md` **(you: download props)**
- [ ] Item pictures for the wardrobe grid

## 6. Admin phase D

- [ ] Content patching: new models and map changes download in game without a new APK
