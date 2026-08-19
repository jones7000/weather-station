{
  description = "Dev shell: PlatformIO toolchain for ESP8266 (Wemos D1 mini) + CC1101 433MHz sniffer";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = import nixpkgs { inherit system; };
    in {
      devShells.${system}.default = pkgs.mkShell {
        packages = with pkgs; [
          platformio
          esptool
          picocom # fallback serial terminal, in case `pio device monitor` misbehaves
          esphome # for esphome/weather-station.yaml (production firmware, HA integration)
        ];

        shellHook = ''
          echo "PlatformIO dev shell ready."
          echo "  pio run -e d1_mini -t upload      # build + flash"
          echo "  pio device monitor -b 115200       # serial monitor"
          echo "  pio device list                    # find the serial port"
          echo "  esphome run esphome/weather-station.yaml   # ESPHome build + flash"
        '';
      };
    };
}
