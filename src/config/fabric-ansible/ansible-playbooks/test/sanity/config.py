import yaml


def load(cfg_file):
    """Reads the global settings file and
     merges it with the specific settings file if specified."""
    with open("config/global.yml", "r") as f:
        settings = yaml.load(f)
    test_settings = {}
    if cfg_file:
        with open(cfg_file, "r") as f:
            test_settings = yaml.load(f)
    for section in test_settings:
        if section in settings:
            settings[section].update(test_settings[section])
        else:
            settings[section] = test_settings[section]
    return settings
# end load
