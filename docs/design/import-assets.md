# Text Twist 2 — Importing the assets

> The end-user should be responsible for importing the assets from the original game.
> On each launch, if the assets hasn't imported yet, will ask the end-user to import it.

## Missing Assets warning dialog

- On each launch (unless the assets are being imported), will ask the user to import from the original game's directory.
- The warning message should be like this: "This program requires you to import the source material from the original game. Otherwise, you won't enjoy the full experience. Besides, the placeholder assets are still on the way."
- The user won't ask again if the assets are imported & verified.

## Import Asset functionality

- Should base on the internal script file named: `scripts/import-original.sh`
- Verify the assets using Checksum (MD5, etc.), and the MD5 strings from the project will be used to check the imported assets.

## References

- https://github.com/hedge-dev/UnleashedRecomp
