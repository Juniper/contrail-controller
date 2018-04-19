import yaml


def load(*cfg_files):
    """Reads the config files and merges them
     in the order in which the files are specified."""
    settings = {}
    for cfg_file in cfg_files:
        with open(cfg_file, "r") as f:
            test_settings = yaml.load(f)
        for section in test_settings:
            if section in settings:
                settings[section].update(test_settings[section])
            else:
                settings[section] = test_settings[section]
    return settings
# end load
