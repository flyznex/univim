class Univim < Formula
  desc "Vim-mode for macOS text fields, plus Vietnamese Telex/VNI input"
  homepage "https://github.com/flyznex/univim"
  head "https://github.com/flyznex/univim.git", branch: "master"
  depends_on :macos

  def install
    system "git", "submodule", "update", "--init", "--recursive"
    system "make", "lib"
    system "make", "app"
    libexec.install "bin/UniVim.app"
    bin.install_symlink libexec/"UniVim.app/Contents/MacOS/univim"
  end

  service do
    run [opt_libexec/"UniVim.app/Contents/MacOS/univim"]
    keep_alive true
    log_path var/"log/univim.log"
    error_log_path var/"log/univim.log"
  end

  test do
    assert_path_exists libexec/"UniVim.app/Contents/MacOS/univim"
  end
end
