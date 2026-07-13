{
  description = "Tip Jar - a Connect-with-Medusa SDK demo (ui_qml, talks to medusa_core)";
  inputs.logos-module-builder.url = "github:logos-co/logos-module-builder";
  outputs = inputs@{ logos-module-builder, ... }:
    logos-module-builder.lib.mkLogosQmlModule {
      src = ./.;
      configFile = ./metadata.json;
      flakeInputs = inputs;
    };
}
