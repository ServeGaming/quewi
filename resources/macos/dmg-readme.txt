Welcome to quewi.


HOW TO INSTALL
==============

  1. Drag quewi.app onto the Applications folder shortcut.
  2. Open your Applications folder.
  3. RIGHT-CLICK (or Control-click) quewi.app and choose Open.
  4. macOS will warn that the developer is unidentified.
     Click Open to confirm. You only do this once.


IF YOU SEE "QUEWI IS DAMAGED AND CAN'T BE OPENED"
=================================================

That's macOS Gatekeeper being overprotective of small open-source
projects that haven't paid Apple's developer fee. The app is fine.

  EASY FIX:  Double-click "Fix Gatekeeper.command" in this window.

  The script clears the quarantine flag from quewi.app in your
  Applications folder and launches it for you. You only need to
  do this once, after the first download.

  The first time you run the .command file, macOS will ask if
  you're sure — right-click → Open does the same dance as the
  app itself.


MANUAL FIX (if you prefer Terminal)
===================================

  xattr -cr /Applications/quewi.app

  Then double-click quewi.app from Applications.


ABOUT
=====

quewi is theatre cueing software — open-source under AGPL-3.0.

  Source and docs:  https://github.com/ServeGaming/quewi
  Report a bug:     https://github.com/ServeGaming/quewi/issues

A future release with an Apple Developer ID will skip the
Gatekeeper dance entirely. Until then, thank you for putting up
with the workaround.
