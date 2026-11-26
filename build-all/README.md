# build-all

Builds several Poli plugins in one go. Run from WSL, with Docker Desktop running:

```
cd /mnt/c/MPC-PLUGINS/build-all
./build.sh                      # asks: which plugins (numbers like "1 3", names, or all) and which drive
./build.sh E all                # no questions: everything, zips to E:\local-plugins\
./build.sh E Poli-q PoliPlug    # no questions: just these
./build.sh none all             # build only, copy nowhere
./build.sh E copy Poli-q        # no build: copy the zip already in Poli-q\dist to the stick (copy all = every one)
```

It runs each plugin's own `build.sh` one after another with the drive you picked (skin, aarch64 build, tests, zip,
copy to the stick). Nothing asks a question after the start, and a failing plugin doesn't stop the rest. At the end a
summary lists each plugin: `OK, on E:`, `BUILT, NOT COPIED` (the stick wasn't there or was write-protected: the zip is in
that plugin's `dist\`), or `FAILED` with its log. Logs: `logs\<date_time>\<plugin>.log` and `summary.txt`.

**New plugins appear here by themselves**: any folder in `C:\MPC-PLUGINS` with a `build.sh` that uses the shared
drive picker (`source "../shared/usb_stick.sh"; pick_drive "$@"` at the start and `copy_to_stick "$ZIP" "<Name>" || exit
1` at the end) is listed. Put an empty file named `.no-build-all` in a folder to leave it out. Old logs can be deleted.
