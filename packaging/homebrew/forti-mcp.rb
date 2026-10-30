# frozen_string_literal: true

# Formula template. `vlr homebrew formula` renders the release url/sha256 into
# release/homebrew/forti-mcp.rb, which `vlr homebrew publish` commits to valkyrianlabs/homebrew-tap.
class FortiMcp < Formula
  desc "FortiOS MCP server: FortiGate discovery, reads and planned changes"
  homepage "https://github.com/valkyrianlabs/forti-mcp"
  url "https://github.com/valkyrianlabs/forti-mcp/releases/download/v0.1.0/forti-mcp-0.1.0.tar.gz"
  sha256 "TODO_REPLACE_WITH_RELEASE_ARCHIVE_SHA256"
  license "MIT"
  head "https://github.com/valkyrianlabs/forti-mcp.git", branch: "main"

  # Only homebrew/core dependencies: Homebrew trusts the formulae named on the command line
  # (brew install valkyrianlabs/tap/forti-mcp) and refuses to load any other formula from an
  # untrusted third-party tap, build dependencies included, even when it pours a bottle. So
  # forti-api is a resource below, as forti-cli is.
  depends_on "meson" => :build
  depends_on "ninja" => :build
  depends_on "pkgconf" => :build

  uses_from_macos "curl"

  # forti-api, the header-only SDK: needed to compile, not at run time. Its release archive; bump it
  # to the latest forti-api release when one is cut.
  resource "forti-api" do
    url "https://github.com/valkyrianlabs/forti-api/releases/download/v1.3.0/forti-api-1.3.0.tar.gz"
    sha256 "218cb631522250bf95182212d88eaa9c4f2d494cd66d6e8d2b236eb1c356ae83"
  end

  # The command engine, compiled in from forti-cli's source at exactly the revision
  # subprojects/forti-cli.wrap pins (packaging/forti-cli-pin.sh keeps the two in step). forti-mcp
  # installs independently: it needs neither the forti-cli formula nor the `forti` executable.
  resource "forti-cli" do
    url "https://github.com/valkyrianlabs/forti-cli/archive/3314e9674a2daada4af513d8ce41a6f40e798aee.tar.gz"
    sha256 "b5f3f87fbd022c9f46b3408750947cd673ba7cd66aa6fe36dd982fc7998cfee2"
  end

  def install
    # Meson uses an existing subprojects/forti-cli as is, so nothing is fetched during the build.
    resource("forti-cli").stage buildpath/"subprojects/forti-cli"

    # The SDK's headers and pkg-config file go into the build tree only, where dependency('forti-api')
    # finds them; none of it reaches the keg.
    resource("forti-api").stage do
      system "meson", "setup", "build", "-Dtests=disabled", *std_meson_args(prefix: buildpath/"forti-api-sdk")
      system "meson", "install", "-C", "build"
    end
    ENV.prepend_path "PKG_CONFIG_PATH", buildpath/"forti-api-sdk/share/pkgconfig"

    # std_meson_args sets --buildtype=release and --wrap-mode=nofallback: forti-api always comes from
    # the resource above, never from subprojects/forti-api.wrap.
    # optimization=3 is meson.build's release profile, repeated so it holds whatever the build type;
    # strip=true installs the binary without its symbol table.
    # The tests need GoogleTest and run in CI; the `test do` block exercises the installed binary.
    system "meson", "setup", "build", "-Dtests=disabled", "-Doptimization=3", "-Dstrip=true", *std_meson_args
    system "meson", "compile", "-C", "build"
    system "meson", "install", "-C", "build", "--skip-subprojects"
  end

  test do
    assert_match "forti-mcp #{version} (engine forti-cli ", shell_output("#{bin}/forti-mcp --version")

    # Offline: an MCP client's handshake and tool listing need no profile, device or network.
    ENV["FORTI_HOME"] = (testpath/"forti").to_s
    requests = [
      { jsonrpc: "2.0", id: 1, method: "initialize",
        params: { protocolVersion: "2025-11-25", capabilities: {}, clientInfo: { name: "brew", version: "1" } } },
      { jsonrpc: "2.0", method: "notifications/initialized" },
      { jsonrpc: "2.0", id: 2, method: "tools/list" },
    ]
    output = pipe_output(bin/"forti-mcp", requests.map { |request| "#{JSON.generate(request)}\n" }.join, 0)
    responses = output.lines.map { |line| JSON.parse(line) }
    assert_equal "2025-11-25", responses.find { |response| response["id"] == 1 }["result"]["protocolVersion"]
    tools = responses.find { |response| response["id"] == 2 }["result"]["tools"].map { |tool| tool["name"] }
    assert_includes tools, "forti_discover"
  end
end
