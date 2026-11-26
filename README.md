# ng-recent

**Status: a first version, not packaged yet.**

What happened lately to your files and folders, as a list any KDE file view can show: `ngrecent:/`.

KDE's "Recent Files" only knows the files an application opened. A picture just pasted into a folder, a download,
a file another program made, a new folder: none of them is there, so the next "Open" dialog (a browser's upload)
cannot offer them. This list has them. [Dolphin NG](https://github.com/Morzilla/dolphin-ng) and
[Dialog NG](https://github.com/Morzilla/dialog-ng) show it in place of "Recent Files" when it is installed.

## What it lists

The latest event of each file and folder in the last two weeks, the latest first:

| Event | How it is known |
|---|---|
| New, New folder | the file's dates: made and not changed since |
| Added | made later than its own content: copied or moved here |
| Modified | changed after it was made |
| Downloaded from *site* | the origin browsers write on a download (`user.xdg.origin.url`) |
| *Action*, from *file* | what Dolphin NG writes on the result of a quick action |
| Opened | KDE's and GTK's recent documents (`recently-used.xbel`) |

It needs nothing running. When the list is asked for it reads the folders people keep their things in (Downloads,
Desktop, Documents, Pictures, Videos, Music, two levels deep, and the home's own files), the desktop's recent
documents, and a journal that programs can add to (`~/.local/share/ng-recent/journal.tsv`).

## Limits

- Only those folders are read: something new elsewhere is listed only if a program wrote it in the journal or an
  application opened it.
- Hidden files and unfinished downloads are left out.
- An entry is the real file: opening, dragging or deleting it acts on the file itself.

## Build

```sh
cmake -B build -DCMAKE_INSTALL_PREFIX=/usr && cmake --build build && sudo cmake --install build
ng-recent-list        # the same list on the terminal
kioclient ls ngrecent:/
```

Needs Qt 6 and KDE Frameworks 6 (KIO, Extra CMake Modules).

## Licence

LGPL-2.1-or-later. The build files are CC0-1.0.
