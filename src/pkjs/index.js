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
        if (theme == "Custom") {
            // after Custom is picked, show color options permanently
            for (var item of clayConfig.getItemsByType("color")) {
                item.show();
            }
        } else {
            colorChangeCausesCustom = false;
            switch (theme) {
            case "Emperor":
                // This should match the defaults in config.json and config.c
                setColor('bgColor',      0xffffff);
                setColor('fgColor',      0x000000);
                setColor('hourColor',    0x000000);
                setColor('minuteColor',  0x000000);
                setColor('comp1FgColor', 0xaa00aa);
                setColor('comp2FgColor', 0xaa00aa);
                setColor('comp3FgColor', 0xaa00aa);
                setColor('comp1BgColor', 0x00ff00);
                setColor('comp2BgColor', 0x00ff00);
                setColor('comp3BgColor', 0x00ff00);
                break;
            case "The World":
                setColor('bgColor',      0x000000);
                setColor('fgColor',      0xffffff);
                setColor('hourColor',    0xffffff);
                setColor('minuteColor',  0xffffff);
                setColor('comp1FgColor', 0x00ff00);
                setColor('comp2FgColor', 0x00ff00);
                setColor('comp3FgColor', 0x00ff00);
                setColor('comp1BgColor', 0xaa00aa);
                setColor('comp2BgColor', 0xaa00aa);
                setColor('comp3BgColor', 0xaa00aa);
                break;
            case "Silver Chariot":
                setColor('bgColor',      0xaaaaaa);
                setColor('fgColor',      0xffffff);
                setColor('hourColor',    0x555555);
                setColor('minuteColor',  0x555555);
                setColor('comp1FgColor', 0x000000);
                setColor('comp2FgColor', 0x000000);
                setColor('comp3FgColor', 0x000000);
                setColor('comp1BgColor', 0x555555);
                setColor('comp2BgColor', 0x555555);
                setColor('comp3BgColor', 0x555555);
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

    clayConfig.on(clayConfig.EVENTS.AFTER_BUILD, function() {
        var themeSelector = clayConfig.getItemByMessageKey("theme");
        for (var item of clayConfig.getItemsByType("color")) {
            item.on("change", function() {  // the "click" event doesn't work
                if (colorChangeCausesCustom) {
                    themeSelector.set("Custom")
                }
            });
        }
        setTheme.call(themeSelector);
        themeSelector.on("change", setTheme);

        var enableFaceText = clayConfig.getItemByMessageKey("faceTextEnabled");
        if (enableFaceText) {
            toggleGroup.call(enableFaceText)
            enableFaceText.on("change", toggleGroup);
        }
        var enableBatteryMeter = clayConfig.getItemByMessageKey("batteryMeterEnabled");
        if (enableBatteryMeter) {
            toggleGroup.call(enableBatteryMeter)
            enableBatteryMeter.on("change", toggleGroup);
        }

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
