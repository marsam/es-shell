{ pkgs ? import <nixpkgs> {} }:

pkgs.stdenv.lib.overrideDerivation pkgs.es (args: {
  src = ./.;
  nativeBuildInputs = args.nativeBuildInputs ++ [ pkgs.autoconf pkgs.automake ];
})
