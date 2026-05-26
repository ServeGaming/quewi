Welcome to quewi.

FIRST LAUNCH
============

  1. Drag quewi.app onto the Applications folder shortcut.
  2. Open your Applications folder.
  3. RIGHT-CLICK (or Control-click) quewi.app and choose Open.
  4. macOS will warn that the developer is unidentified.
     Click Open to confirm. You only do this once.


IF YOU SEE "QUEWI IS DAMAGED AND CAN'T BE OPENED"
=================================================

That message means macOS stripped the ad-hoc signature when it
applied its download quarantine flag. It's a Gatekeeper false
positive, not an actual problem with quewi. To clear the flag:

  Open Terminal and run:

      xattr -cr /Applications/quewi.app

  Then double-click quewi.app again. The friendlier "unidentified
  developer" prompt will appear; right-click → Open will get you in.

You only need to do this once, after the first download. A future
release with a real Apple Developer ID will skip this dance
entirely — for now, this is the open-source-project workaround.


ABOUT
=====

quewi is theatre cueing software — open-source under AGPL-3.0.

  Source and documentation:  https://github.com/ServeGaming/quewi
  Report a bug:              https://github.com/ServeGaming/quewi/issues
