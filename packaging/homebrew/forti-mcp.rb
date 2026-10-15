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

  # Header-only SDK: needed to compile, not at run time.
  depends_on "forti-api" => :build
  depends_on "meson" => :build
  depends_on "ninja" => :build
  depends_on "pkgconf" => :build

  uses_from_macos "curl"

  # The command engine, compiled in from forti-cli's source at exactly the revision
  # subprojects/forti-cli.wrap pins (packaging/forti-cli-pin.sh keeps the two in step). forti-mcp
  # installs independently: it needs neither the forti-cli formula nor the `forti` executable.
  resource "forti-cli" do
    url "https://github.com/valkyrianlabs/forti-cli/archive/ecacc679d81880b194eb2472ecf42d179c4632d8.tar.gz"
    sha256 "9d10e500726ddbbe23e89346236720ec636bd25244e0b1f7d8102497019a1b6f"
  end

  def install
    # Meson uses an existing subprojects/forti-cli as is, so nothing is fetched during the build.
    resource("forti-cli").stage buildpath/"subprojects/forti-cli"

    # std_meson_args sets --buildtype=release and --wrap-mode=nofallback: forti-api always comes from
    # the forti-api formula, never from subprojects/forti-api.wrap.
    # optimization=3 is meson.build's release profile, repeated so it holds whatever the build type;
    # strip=true installs the binary without its symbol table.
    # The tests need GoogleTest and run in CI; the `test do` block exercises the installed binary.
    system "meson", "setup", "build", "-Dtests=disabled", "-Doptimization=3", "-Dstrip=true", *std_meson_args
    # Each engine shard translation unit needs about 2 GB of RAM: cap the jobs so an 8 GB Mac
    # does not swap.
    system "meson", "compile", "-C", "build", "--jobs", [ENV.make_jobs, 4].min.to_s
    system "meson", "install", "-C", "build", "--skip-subprojects"
  end

  test do
    assert_match "forti-mcp #{version} (engine forti-cli ", shell_output("#{bin}/forti-mcp --version")

    # Offline: an MCP client's handshake and tool listing need no profile, device or network.
    ENV["FORTI_CONFIG_DIR"] = (testpath/"config").to_s
    ENV["XDG_STATE_HOME"] = (testpath/"state").to_s
    ENV["XDG_CACHE_HOME"] = (testpath/"cache").to_s
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
