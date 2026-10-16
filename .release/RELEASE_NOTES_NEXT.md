<!--
vl-release staged release notes: the user-facing story of the NEXT release.
Maintained continuously while working; `vlr prepare` promotes it into the release notes
history (and the GitHub release) and resets this file to this template.

Format: the first line is "# <release title>" WITHOUT a version number (vlr adds it).
Everything after the title is the Markdown release body. Describe the resulting behavior
for users and operators; keep it representative of what actually ships.
-->
# Bottles for every Mac on macOS 15 or later

- **Homebrew pours a bottle on macOS 15 or later, on Apple silicon and Intel.** forti-mcp 0.4.0 could
  not be built on macOS 15, because forti-api 1.1.0 needed Xcode 26's C++ library; this release is
  built on forti-api 1.1.1, which fixes that, with forti-cli 0.4.2's engine.
- **The `.deb` installs on its own again.** It recommends `valkyrianlabs-archive-keyring` instead of
  depending on it: installs from the APT repository still get the keyring, and the package from a
  GitHub release installs without the repository.

