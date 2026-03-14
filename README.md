# JPEGView (L-Fork)

aka JPEGView Linear

## Core differences to the original version 

* Up- and downsamples images in linear space. This is beneficial for lineart, where lines and edges otherwise come out too dark and screentones can show moiree patterns that don't exist in the actual image. This is of course also a problem with photographs, where image detail of the zoomed down on screen image will come out too dark, and while professional image editing tools like Photoshop and GIMP have been using linear light scaling for a long time, very few image viewers exist that actually give correct output, sticking to a incorrect but faster method of calculation instead. JPEGView_L uses multi-threading and SSE/AVX2 extensions to maintain a high resampling speed (~70% of IrfanView) while not cutting any corners quality-wise.
More information: http://www.ericbrasseur.org/gamma.html
* The selection of available downsampling filters have been extended to offer the well understood 'Hermite', 'Mitchell', 'Catrom' for INI setting "DownSamplingFilter".
More information on how resampling filters work: http://legacy.imagemagick.org/Usage/filter/
* Support for CBZ and CB7 comic book archive format. Shoutout to [sdneon](https://github.com/sdneon/jpegview) who came up with the implementation
* Bookmark support for CBZ/CB7 files. Those archives are automatically opened on the page you last closed them on. Bookmarks are stored in "%AppData%\JPEGView\Bookmarks.ini"
* Custom zoom size for Book Mode as percentage of window height (new INI setting: BookModePageHeight)
* One instance per folder mode (INI setting: SingleInstance=PerFolder)
* Smooth scrolling with vsync for panning keys (new INI setting: SmoothPanning=true)
* Smart panning keys that combine scrolling and jumping to next/previous image based on zoom (new INI setting: SmartPanningKeys=true)
* Allows using the default file type icons on your system as the JPEGView window icon (new INI setting: TitleBarUseFileIcon=true)
* Checkerboard background (instead of solid color) mode for files with transparency channel (new INI setting: TransparencyMode=Checkerboard). Implementation again done by [sdneon](https://github.com/sdneon/jpegview) for his own fork.
* Improved caching. Keeps the 2 images before and after the currently visible in cache. (new INI setting: CacheRange=2)
* Settings default to a more viewer centric workflow. Just the image - no Bottom Panel, Navigation Panel or Zoom Navigator overlay by default. (Note: This fork uses the same settings file location as the upstream version, "%AppData%\JPEGView", so if an older setting file already exists there, it will use the older settings)

## System Requirements

* 64-bit Windows 7 or higher
* CPU with with SSE2 or higher (support for AVX2 would be ideal)

## Additional Thanks

* To aviscaerulea for the optimized alpha blending code

<br><br>

---

# JPEGView - Image Viewer and Editor

This is the official re-release of JPEGView.

## Description

JPEGView is a lean, fast and highly configurable image viewer/editor with a minimal GUI.

### Formats Supported

JPEGView has built-in support the following formats:

* Popular: JPEG, GIF
* Lossless: BMP, PNG, TIFF, PSD
* Web: WEBP, JXL, HEIF/HEIC, AVIF
* Legacy: TGA, WDP, HDP, JXR
* Camera RAW formats:
  * Adobe (DNG), Canon (CRW, CR2, CR3), Nikon (NEF, NRW), Sony (ARW, SR2)
  * Olympus (ORF), Panasonic (RW2), Fujifilm (RAF)
  * Sigma (X3F), Pentax (PEF), Minolta (MRW), Kodak (KDC, DCR)
  * A full list is available here: [LibRaw supported cameras](https://www.libraw.org/supported-cameras)

Many additional formats are supported by Windows Imaging Component (WIC)

### Basic Image Editor

Basic on-the-fly image processing is provided - allowing adjusting typical parameters:

* sharpness
* color balance
* rotation
* perspective
* contrast
* local under-exposure/over-exposure

### Other Features

* Small and fast, uses AVX2/SSE2 and up to 4 CPU cores
* High quality resampling filter, preserving sharpness of images
* Basic image processing tools can be applied realtime during viewing
* Movie/Slideshow mode - to play folder of JPEGs as movie

# Installation

## Official Releases

Official releases will be made to [sylikc's GitHub Releases](https://github.com/sylikc/jpegview/releases) page.  Each release includes:

* **Archive Zip/7z** - Portable
* **Windows Installer MSI** - For Installs
* **Source code** - Build it yourself

## Portable

JPEGView _does not require installation_ to run.  Just **unzip, and run** either the 64-bit version, or the 32-bit version depending on which platform you're on.  It can save the settings to the extracted folder and run entirely portable.

## MSI Installer

For those who prefer to have JPEGView installed for All Users, a 32-bit/64-bit installer is available to download starting with v1.0.40.

(Unfortunately, I don't own a code signing certificate yet, so the MSI release is not signed.  Please verify checksums!)

### WinGet

If you're on Windows 11, or Windows 10 (build 1709 or later), you can also download it directly from the official [Microsoft WinGet tool](https://docs.microsoft.com/en-us/windows/package-manager/winget/) repository.  This downloads the latest MSI installer directly from GitHub for installation.

Example Usage:

C:\> `winget search jpegview`
```
Name     Id              Version  Source
-----------------------------------------
JPEGView sylikc.JPEGView 1.1.43  winget
```

C:\> `winget install jpegview`
```
Found JPEGView [sylikc.JPEGView] Version 1.1.43
This application is licensed to you by its owner.
Microsoft is not responsible for, nor does it grant any licenses to, third-party packages.
Downloading https://github.com/sylikc/jpegview/releases/download/v1.1.43/JPEGView64_en-us_1.1.43.msi
  ██████████████████████████████  4.23 MB / 4.23 MB
Successfully verified installer hash
Starting package install...
Successfully installed
```

## PortableApps

Another option is to use the official [JPEGView on PortableApps](https://portableapps.com/apps/graphics_pictures/jpegview_portable) package.  The PortableApps launcher preserves user settings in a separate directory from the extracted application directory.  This release is signed.

## Scoop

[Scoop](https://scoop.sh/) is a Windows command-line installer and manager for portable applications.

To install with Scoop, run the following commands:

```shell
scoop bucket add extras
scoop install extras/jpegview-fork
```

After installation, the configuration file is located at `%UserProfile%\scoop\persist\JPEGView-fork\JPEGView.ini`.

## System Requirements

* 64-bit version: Windows 7/8/10/11 64-bit or later
* 32-bit version: Windows 7 or later
  * A special _32-bit Windows XP SP2_ build is available, which supports most formats (except for formats added after v1.0.37.1, ex. Animated PNG, JXL, HEIC).  Other features and options are the same as the normal builds.

## What's New

* See what has changed in the [latest releases](https://github.com/sylikc/jpegview/releases)
* Or Check the [CHANGELOG.txt](https://github.com/sylikc/jpegview/blob/master/CHANGELOG.txt) to review new features in detail.

# Localization

By default, the language is auto-detected to match your Windows Locale.  All the text in the menus and user interface should show in your language.  To override the auto-detection, manually set `Language` option in `JPEGView.ini`

JPEGView is currently translated/localized to 28 languages:

| INI Option | Language |
| ---------- | -------- |
| be | Belarusian |
| bg | Bulgarian |
| cs | Czech |
| de | German |
| el | Greek, Modern |
| es-ar | Spanish (Argentina) |
| es | Spanish |
| eu | Basque |
| fi | Finnish |
| fr | French (Français) |
| hu | Hungarian |
| it | Italian |
| ja | Japanese (日本語) |
| ko | Korean (한국어) |
| pl | Polish |
| pt-br | Portuguese (Brazilian) |
| pt | Portuguese |
| ro | Romanian |
| ru | Russian (Русский) |
| sk | Slovak |
| sl | Slovenian (Slovenščina) |
| sr | Serbian (српски) |
| sv | Swedish |
| ta | Tamil |
| tr | Turkish (Türkçe) |
| uk | Ukrainian (Українська) |
| zh-tw | Chinese, Traditional (繁體中文) |
| zh | Chinese, Simplified (简体中文) |

See the [Localization wiki page](https://github.com/sylikc/jpegview/wiki/Localization#localization-status) for translation status for each language.

# Help / Documentation

The JPEGView documentation is a little out of the date at the moment, but should still give a good summary of the features.

This [readme.html](https://htmlpreview.github.io/?https://github.com/sylikc/jpegview/blob/master/src/JPEGView/Config/readme.html) is part of the JPEGView package.

# Brief History

This GitHub repo continues the legacy (is a "fork") of the excellent project [JPEGView by David Kleiner](https://sourceforge.net/projects/jpegview/).  Unfortunately, starting in 2020, the SourceForge project has essentially been abandoned, with the last update being [2018-02-24 (1.0.37)](https://sourceforge.net/projects/jpegview/files/jpegview/).  It's an excellent lightweight image viewer that I use almost daily!

The starting point for this repo was a direct clone from SourceForge SVN to GitHub Git.  By continuing this way, it retains all previous commits and all original author comments.

I'm hoping with this project, some devs might help me keep the project alive!  It's been awhile, and could use some new features or updates.  Looking forward to the community making suggestions, and devs will help with some do pull requests as some of the image code is quite a learning curve for me to pick it up. -sylikc

## Special Thanks

Special thanks to [qbnu](https://github.com/qbnu) for adding additional codec support!
* Animated WebP
* Animated PNG
* JPEG XL with animation support
* HEIF/HEIC/AVIF support
* QOI support
* ICC Profile support for WebP, JPEG XL, HEIF/HEIC, AVIF
* LibRaw support (all updated RAW formats, such as CR3)
* Photoshop PSD support

Thanks to all the _translators_ which keep JPEGView strings up-to-date in different languages!  See [CHANGELOG.txt](https://github.com/sylikc/jpegview/blob/master/CHANGELOG.txt) to find credits for translators at each release!
