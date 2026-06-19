var Clay = require('@rebble/clay');
var clayConfig = require('./config.json');
var clay = new Clay(clayConfig, function(minified) {
    var clayConfig = this;
    var colorChangeCausesCustom = true;

    function setColor(messageKey, color) {
        var item = clayConfig.getItemByMessageKey(messageKey);
        if (item) {
            item.set(color);
        }
    }

    function setTheme() {
        var themeSelector = this;
        const theme = themeSelector.get();
        colorChangeCausesCustom = false;
        switch (theme) {
        case "Emperor":
            // This should match the defaults in config.json and config.c
            setColor('bgColors',      0xffffff);
            setColor('fgColors',      0x000000);
            setColor('hourColor',     0x000000);
            setColor('minuteColor',   0x000000);
            setColor('secondColor',   0x000000);
            setColor('compFgColors',  0xaa00aa);
            setColor('compBgColors',  0x00ff00);
            setColor('faceTextColor', 0xaaaaaa);
            break;
        case "The World":
            setColor('bgColors',      0x000000);
            setColor('fgColors',      0xffffff);
            setColor('hourColor',     0xffffff);
            setColor('minuteColor',   0xffffff);
            setColor('secondColor',   0xffffff);
            setColor('compFgColors',  0x00ff00);
            setColor('compBgColors',  0xaa00aa);
            setColor('faceTextColor', 0x555555);
            break;
        case "Silver Chariot":
            setColor('bgColors',      0xaaaaaa);
            setColor('fgColors',      0xffffff);
            setColor('hourColor',     0x555555);
            setColor('minuteColor',   0x555555);
            setColor('secondColor',   0x555555);
            setColor('compFgColors',  0x000000);
            setColor('compBgColors',  0x555555);
            setColor('faceTextColor', 0xffffff);
            break;
        // case "Star Platinum":
        // case "Hermit Purple":
        // case "Magician's Red":
        // case "Hierophant Green":
        // case "Yellow Temperance":
        // case "Ebony Devil":
        // case "Dark Blue Moon":
        // case "Sun":

        // dark blue and yellow
        default:
            break;
        }
        colorChangeCausesCustom = true;
    }

    // Handler for a Complication's "when to show" component, to hide/show the other components
    function toggleCompMode() {
        for (var item of clayConfig.getItemsByGroup(this.messageKey + "ToggleGroup")) {
            if (this.get() === "0") {
                item.hide();
            } else {
                item.show();
            }
        }

        toggleCompType.call(clayConfig.getItemByMessageKey(this.messageKey.replace("Mode", "Type")))
    }

    function setAdvancedColor() {
        is_advanced = this.get()
        for (var item of clayConfig.getItemsByGroup("advancedColorGroup")) {
            if (is_advanced) {
                item.show();
            } else {
                item.hide();
            }
        }
        for (var item of clayConfig.getItemsByGroup("simpleColorGroup")) {
            if (is_advanced) {
                item.hide();
            } else {
                item.show();
            }
        }
    }

    function setBgColors() {
        setColor("bgColor", this.get())
    }
    function setFgColors() {
        setColor("fgColor", this.get())
        setColor("hourColor", this.get())
        setColor("minuteColor", this.get())
        setColor("secondColor", this.get())
    }
    function setCompFgColors() {
        setColor("comp1FgColor", this.get())
        setColor("comp2FgColor", this.get())
        setColor("comp3FgColor", this.get())
    }
    function setCompBgColors() {
        setColor("comp1BgColor", this.get())
        setColor("comp2BgColor", this.get())
        setColor("comp3BgColor", this.get())
    }

    // Handler for Complications' Types component, to hide/show specific comp type options
    function toggleCompType() {
        if (this.get() === "8") {
            clayConfig.getItemById(this.messageKey + "TimezoneText").show();
            clayConfig.getItemById(this.messageKey + "TimezoneValue").show();
        } else {
            clayConfig.getItemById(this.messageKey + "TimezoneText").hide();
            clayConfig.getItemById(this.messageKey + "TimezoneValue").hide();
        }
    }

    // Handler for a Toggle component, to show/hide Components with a matching group
    function toggleGroup() {
        for (var item of clayConfig.getItemsByGroup(this.messageKey + "ToggleGroup")) {
            if (this.get()) {
                item.show();
            } else {
                item.hide();
            }
        }
    }
    function register_toggle_group(toggle_message_key) {
        var toggle = clayConfig.getItemByMessageKey(toggle_message_key);
        if (toggle) {
            toggleGroup.call(toggle)
            toggle.on("change", toggleGroup);
        }
    }

    clayConfig.on(clayConfig.EVENTS.AFTER_BUILD, function() {
        var themeSelector = clayConfig.getItemByMessageKey("theme");
        for (var item of clayConfig.getItemsByType("color")) {  // TODO dont need this
            item.on("change", function() {  // the "click" event doesn't work
                if (colorChangeCausesCustom) {
                    themeSelector.set("Custom")
                }
            });
        }
        setTheme.call(themeSelector);
        themeSelector.on("change", setTheme);

        var advancedColorToggle = clayConfig.getItemByMessageKey("advancedColor");
        setAdvancedColor.call(advancedColorToggle);
        advancedColorToggle.on("change", setAdvancedColor);

        clayConfig.getItemByMessageKey("bgColors").on("change", setBgColors);
        clayConfig.getItemByMessageKey("fgColors").on("change", setFgColors);
        clayConfig.getItemByMessageKey("compFgColors").on("change", setCompFgColors);
        clayConfig.getItemByMessageKey("compBgColors").on("change", setCompBgColors);

        register_toggle_group("faceTextEnabled");
        register_toggle_group("batteryMeterEnabled");

        for (var compMode of clayConfig.getItemsByGroup("compModes")) {
            toggleCompMode.call(compMode)
            compMode.on("change", toggleCompMode);
        }
        for (let i = 1; i <= 3; i++) {
            var compType = clayConfig.getItemByMessageKey("comp" + i.toString() + "Type");
            if (compType) {
                toggleCompType.call(compType)
                compType.on("change", toggleCompType);
            }
        }
    });

});

// TODO clay.registerComponent(require('./yoshiColorPicker'));
// or https://github.com/jrmobley/pebble-clay-preview-component
