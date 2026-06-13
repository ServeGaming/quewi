# Import from URL (media downloader)

Quewi can pull audio and video straight into a show from YouTube and
~1800 other sites, powered by [yt-dlp](https://github.com/yt-dlp/yt-dlp).
**Cue → Import from URL…** (Ctrl+U).

---

## What it does

1. **Search** — type a phrase to search YouTube, or paste any video /
   playlist URL.
2. **Preview** — audio previews stream inside quewi (no download);
   video opens the source page in your browser.
3. **Download + add cue** — pick Audio or Video, download into the
   show's `media/` folder, and a matching Audio or Video cue is added
   to the current list automatically.

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
- [ ] In-app video preview (currently opens the browser).
- [ ] Format/quality picker (resolution, codec, bitrate).
- [ ] Clip trimming on download (`--download-sections`).
