# s3fuse #

s3fuse is a FUSE (Filesystem in Userspace) driver for Amazon S3 and Google Cloud Storage.

## What's New ##

_2013-11-24_: Version 0.15 released with minor updates:

  * Removed libxml++ dependency.
  * Fixed libcurl init/cleanup bug.
  * Removed known-issue notes on MIME mapping and Google Cloud Storage Manager folder representation incompatibility.

> Since [Google Code downloads are deprecated](http://google-opensource.blogspot.com/2013/05/a-change-to-google-code-download-service.html), this and future s3fuse releases will be hosted here:
> https://drive.google.com/folderview?id=0B071t7_clY1HWFpmNDMwUmQ0bms&usp=sharing#list

> Ubuntu packages are here:
> https://launchpad.net/~s3fuse/+archive/stable

_2013-04-28_: Version 0.14 released.

  * **NEW: Multipart (resumable) uploads for Google Cloud Storage.**
  * **NEW: Support for FV/S.**
  * **NEW: Set file content type by examining extension.**
  * **NEW: Set Cache-Control header with extended attribute.**
  * **NEW: Allow creation of "special" files (FIFOs, devices, etc.).**
  * Allow verbosity to be specified explicitly on the command line.
  * Fix multipart download bug that prevented some transfers from resuming.
  * Use real user ID/group ID rather than effective IDs.
  * Only use user ID/group ID/mode in object metadata if values are valid.
  * Set and retrieve object creation time (on newly-created objects).
  * Fix inconsistent state bug when reopening file immediately after closing.
  * Various POSIX compliance fixes.

> Ubuntu packages are here:
> https://launchpad.net/~s3fuse/+archive/stable

_2013-02-15_: Version 0.13.1 released.

> This release contains a minor bug fix for the OS X app bundle.  Debian and Red Hat packages are unaffected.

_2013-02-10_: Version 0.13 released.

  * **NEW: File encryption. See README.**
  * **NEW: Glacier restore requests. See README.**
  * **NEW: Add least-recently-used eviction to object cache, controlled by the max\_objects\_in\_cache configuration option.**
  * **NEW: OS X binaries.**
  * _IMPORTANT: Removed "auth\_data" configuration option. For AWS, use "aws\_secret\_file" instead. For Google Storage, use "gs\_token\_file"._
  * _IMPORTANT: Search for configuration file in ~/.s3fuse/s3fuse.conf before trying %sysconfdir/s3fuse.conf (this is usually /etc/s3fuse.conf or /usr/local/s3fuse.conf)._
  * Switched to SHA256 for file integrity check. File hash, if available, will be in the "s3fuse\_sha256" extended attribute.
  * Added standalone SHA256 hash generator ("s3fuse\_sha256\_hash") that uses the same hash-list method as s3fuse.
  * Changed handling of last-modified times. Some timestamps may be inaccurate (but will be correct when updated).
  * Renamed built-in extended attributes "content\_type" and "etag" to "s3fuse\_content\_type" and "s3fuse\_etag", respectively.
  * Removed "md5" extended attribute.
  * Added note about daemon\_timeout.
  * Removed xattr\_prefix configuration option and made "user." standard (Linux only).
  * Added event counters/statistics ("stats\_file" config option).
  * Made "noappledouble" and "daemon\_timeout=3600" default options on OS X.
  * Various robustness fixes.
  * Add manual pages.

> As with release 0.12, Ubuntu packages are here:
> https://launchpad.net/~s3fuse/+archive/stable

_2012-09-20_: [Scott R.](https://launchpad.net/~scottritchie) has rather helpfully created a PPA for Ubuntu s3fuse packages:

> https://launchpad.net/~s3fuse/+archive/stable

_2012-09-17_: Version 0.12 released.

  * **NEW: MacOS support. See README.**
  * **NEW: Made AWS endpoint configurable (to support non-US buckets). See README.**
  * Set mount point UID/GID to process effective UID/GID.
  * Fixed extended attribute bugs.
  * Added support for curl/NSS.
  * Fixed circular ownership bug that was causing FDs to be leaked.
  * Made requirements compatible with RHEL6/Amazon Linux.
  * Fixed memory leak in CURL request code.

_2011-07-15_: Version 0.11 released.

  * **NEW: Added [Google Storage](http://code.google.com/apis/storage/) support.**
  * NEW: Moved AWS keys out of s3fuse.conf. See README.
  * NEW: Changed required fields in s3fuse.conf. See README.
  * On rename, remove destination rather than failing with -EEXIST.
  * Improved error handling in FUSE init/pre-init.
  * Invalidate parent in directory cache when child is modified.
  * Fixed request error handling.
  * Increased default request timeout to 30 seconds (from 10).
  * Changed default logging level to LOG\_WARNING instead of LOG\_ERR.

## Goals ##

I wrote s3fuse with the following goals/objectives in mind:

  * Thread safety -- concurrent operations should behave according to the principle of least astonishment.
  * Compatibility with other S3 applications, including Amazon's web-based S3 browser.
  * Improved upload/download performance (by using multipart transfers).
  * Support for extended file attributes.
  * Support for consistency checking (using S3's MD5-testing features).

## Warning ##

s3fuse is alpha-level software and **may cause data loss, may corrupt data, may lead to system instability, and may eat your kittens**. Be careful, and be realistic.

## Inspiration ##

s3fuse was heavily inspired by the [s3fs](http://code.google.com/p/s3fs) project.