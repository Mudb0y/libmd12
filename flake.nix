{
  description = "libmd12hundwed: a faithful emulation of the E-mu SP-1200's audio path";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { nixpkgs, ... }:
    let
      pkgs = nixpkgs.legacyPackages.x86_64-linux;
    in
    {
      # The library itself needs only a C99 compiler. The rest is for checking
      # it: ngspice for the filter circuits, the ARM compiler and QEMU to prove
      # it runs on a Cortex-M, and Python for the tables and the analysis.
      devShells.x86_64-linux.default = pkgs.mkShell {
        packages = [
          pkgs.clang
          pkgs.gnumake
          pkgs.gcc-arm-embedded
          pkgs.qemu
          pkgs.ngspice
          (pkgs.python3.withPackages (ps: [ ps.numpy ps.scipy ps.sympy ]))
        ];
      };
    };
}
