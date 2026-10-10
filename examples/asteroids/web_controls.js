/* Reserve source shortcuts only while this example's canvas has keyboard focus.
 * Native UI buttons remain the primary browser-safe alternative. */
(function () {
    var before = Module['preRun'];
    Module['preRun'] = Array.isArray(before) ? before : (before ? [before] : []);
    Module['preRun'].push(function () {
        var canvas = Module['canvas'];
        if (!canvas) return;
        canvas.addEventListener('keydown', function (event) {
            if (event.ctrlKey && !event.altKey && !event.metaKey &&
                (event.code === 'KeyP' || event.code === 'KeyL')) {
                event.preventDefault();
            }
        });
    });
}());
