# Signing key

`release.jks` signs every build of this app.

    alias:     harmonizer
    password:  harmonizer  (both store and key)
    validity:  30 years, from 2026-09-14
    SHA-256:   37:E5:94:23:B5:6C:B7:71:FC:FA:64:C7:BB:F3:5F:7C:
               A4:72:C4:8C:91:AB:20:57:B2:F9:21:C2:BB:65:8A:E1

## Why it is here, and what that costs

Android will only install an update over an existing app when both are signed by
the same key. Before this key existed, CI signed each build with a throwaway
debug key generated fresh on the runner, so every build had a different
signature and every install required uninstalling the previous one first. That
also made in-app updates impossible.

This keystore is committed to the repository so CI can sign with it without any
setup. **The repository is public, so this signing key is public too.** The
password above is not a secret and is not doing any work.

What that actually means: anyone can build an APK that this phone would accept
as a legitimate update to this app. That matters if a stranger can get you to
install an APK they made. For an app installed on one person's phone from a
release page they control, the exposure is small — but it is real, and it is the
reason production apps keep this file out of the repository.

## If you ever distribute this to anyone else

Rotate to a key that is not public, before you share it:

1. Generate a fresh keystore:

       keytool -genkeypair -v -keystore release.jks -storetype PKCS12 \
         -alias harmonizer -keyalg RSA -keysize 4096 -validity 10950 \
         -storepass <strong password> -keypass <strong password> \
         -dname "CN=Harmonizer"

2. `base64 -w0 release.jks` and put the result in a repository secret named
   `KEYSTORE_BASE64`, and the password in `KEYSTORE_PASSWORD`
   (Settings → Secrets and variables → Actions).
3. Have the workflow decode it to `keystore/release.jks` before `assembleRelease`,
   and delete this committed copy.

Changing the key means everyone on the old one has to uninstall and reinstall
once — including you. Rotate before you hand the app to anybody, not after.
