-- CPack DragNDrop setup script.
--
-- Runs against the mounted DMG volume during cpack -G DragNDrop.
-- Job: turn the bare "open as folder" view CPack produces into a
-- proper drag-to-install layout — quewi.app on the left, an
-- /Applications shortcut on the right, big icons, no toolbar.
--
-- CPack invokes this script with the mounted volume name as the
-- first positional parameter.

on run argv
    set volumeName to item 1 of argv

    tell application "Finder"
        tell disk volumeName
            open

            -- Make the /Applications shortcut. Real symlink so a drag
            -- from inside the DMG to that icon copies into the user's
            -- /Applications folder, which is the standard mac flow.
            -- 'do shell script' runs while the volume is read-write
            -- (CPack flips it RO after this script finishes).
            try
                do shell script "ln -s /Applications " & quoted form of ("/Volumes/" & volumeName & "/Applications")
            on error errMsg
                -- Symlink may already exist on retry; not fatal.
            end try

            -- Visual chrome: icon view, hidden toolbar/status bar,
            -- generous window so both icons sit comfortably with a
            -- visible arrow between them.
            set current view of container window to icon view
            set toolbar visible of container window to false
            set statusbar visible of container window to false
            set bounds of container window to {200, 120, 760, 460}

            set theViewOptions to icon view options of container window
            set arrangement of theViewOptions to not arranged
            set icon size of theViewOptions to 128
            set text size of theViewOptions to 13

            -- Position the .app on the left, /Applications on the right.
            -- 560-wide window centres at x=280; place icons ~190 apart
            -- so the arrow between them reads "drag from here to here".
            try
                set position of item "quewi.app" of container window to {140, 170}
            end try
            try
                set position of item "Applications" of container window to {420, 170}
            end try

            update without registering applications
            delay 1
            close
        end tell
    end tell
end run
