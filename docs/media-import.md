# Import from URL (media downloader)

Quewi can pull audio and video straight into a show from YouTube and
~1800 other sites, powered by [yt-dlp](https://github.com/yt-dlp/yt-dlp).
There are two ways in:

- **Cue list:** **Cue → Import from URL…** (`Ctrl+U`) adds an Audio or
  Video cue after the selected cue.
- **Soundboard** *(coming in 1.0.2)*: right-click an **empty pad** →
  **Import from URL…** downloads the sound straight onto that pad. It's
  the same search, preview and download, audio only.

---

## What it does

1. **Search** — type a phrase to search YouTube, or paste any video /
   playlist URL.
2. **Preview** — plays inside quewi without downloading, with a level
   meter and a scrub bar. Video previews show the picture as well.
3. **Download + add cue** — pick Audio or Video, download into the
   show's `media/` folder, and a matching Audio or Video cue is added
   to the current list automatically. From a pad, **Download to pad**
   puts it on the pad instead.
4. **Trim (optional, coming in 1.0.2)** — tick **Open it in the audio editor afterwards
   to trim it** to set the in and out points straight away. That's handy
   for grabbing one effect out of a long compilation. The download itself
   is kept whole. The option is on by default for pads and off for the
   cue list, and quewi remembers your choice for each.

---

## How the downloader is managed

- **No bundled binary.** On first use quewi downloads the latest
  yt-dlp standalone build into its app-data folder
  (`<AppData>/quewi/tools/`). The installer stays small and the tool
  stays current — yt-dlp updates frequently as sites change their
  internals.
- **Self-update.** The "Update downloader" link re-fetches the latest
  yt-dlp. Run it if downloads start failing after a site change.
- **No ffmpeg required (today).** V1 requests single pre-muxed streams
  (`bestaudio` for audio, `best[ext=mp4]` for video), so no
  stream-merging step is needed. Downloaded audio (often Opus/WebM)
  plays directly because quewi forces Qt's FFmpeg media backend.
  *Higher-resolution video that requires merging separate video+audio
  streams needs ffmpeg and is a planned follow-up.*

---

## Where files land

- **Saved show:** a `media/` folder next to the `.quewi` file, so the
  show stays self-contained and portable.
- **Untitled show:** `~/Music/quewi-imports/`.

---

## Legal

Downloading copyrighted material without permission, or in violation
of a site's terms of service, may be unlawful where you live. Quewi
shows a one-time disclaimer the first time you open the importer and
puts the responsibility on you to use it only for material you're
licensed to perform with. The tool is a neutral media importer (it
works with Vimeo, SoundCloud, archive.org, direct links, and many
more), not a YouTube-specific ripper.

---

## Roadmap

- [ ] Bundle/download ffmpeg to unlock full-resolution video (merged
      streams) and audio transcode-to-WAV for zero-latency GO.
- [ ] Format/quality picker (resolution, codec, bitrate).
- [ ] Download only the trimmed section (`--download-sections`) instead of
      the whole file. Today you trim after downloading.
- [ ] Freesound.org as a source for Creative Commons sound effects.
