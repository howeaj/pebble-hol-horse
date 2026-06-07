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

    // function setColorById(id, color) {
    //     var item = clayConfig.getItemById(id);
    //     if (item) {
    //         item.set(color);
    //     }
    // }

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

            // dark blue and yellow
            default:
                break;
            }
            colorChangeCausesCustom = true;
        }
    }

    // function toggleTouchOptions() {
    //     var enableTouch = this;
    //     for (var item of clayConfig.getItemsByGroup("touch")) {
    //         if (enableTouch.get()) {
    //             item.show();
    //         } else {
    //             item.hide();
    //         }
    //     }
    // }

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

        // var enableTouch = clayConfig.getItemByMessageKey("enableTouch");
        // if (enableTouch) {
        //     toggleTouchOptions.call(enableTouch)
        //     enableTouch.on("change", toggleTouchOptions);
        // }
    });

});
