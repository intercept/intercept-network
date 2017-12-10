class CfgPatches {
    class intercept_network {
        units[] = {""};
        weapons[] = {};
        requiredVersion = 0.1;
        requiredAddons[] = { "Intercept_Core" };
        version = 0.1;
    };
};

class Intercept {
    class core {
        class intercept_network {
			certificate = "core";
            pluginName = "intercept-network";
        };
    };
};