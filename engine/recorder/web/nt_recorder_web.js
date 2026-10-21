// nt_recorder web backend: WebCodecs H.264 + AAC from the canvas, muxed to MP4 by Mediabunny.
// Loaded with --extern-pre-js after deps/mediabunny/nt_mediabunny.js, outside Closure, so the
// WebCodecs and Mediabunny property names stay intact in wasm-release.
var NtRecorder = (function () {
    var cur = null;

    function open(name, canvas, w, h, fps) {
        if (typeof VideoEncoder === 'undefined' || typeof AudioEncoder === 'undefined' || typeof NtMediabunny === 'undefined') {
            return false;
        }
        var MB = NtMediabunny;
        var r = { name: name, canvas: canvas, w: w, h: h, fps: fps, error: null, samples: 0 };
        var fail = function (e) { if (!r.error) { r.error = String(e); } };
        try {
            r.out = new MB.Output({ format: new MB.Mp4OutputFormat(), target: new MB.BufferTarget() });
            r.vsrc = new MB.EncodedVideoPacketSource('avc');
            r.asrc = new MB.EncodedAudioPacketSource('aac');
            r.out.addVideoTrack(r.vsrc, { frameRate: fps });
            r.out.addAudioTrack(r.asrc);
            // Packets join the muxer strictly in output order through one promise chain.
            r.chain = r.out.start();
            var add = function (src) {
                var first = true;
                return function (chunk, meta) {
                    var p = MB.EncodedPacket.fromEncodedChunk(chunk);
                    var m = first ? meta : undefined;
                    first = false;
                    r.chain = r.chain.then(function () { return src.add(p, m); });
                };
            };
            // Lowest H.264 High level that fits the macroblock size and rate.
            var mbs = Math.ceil(w / 16) * Math.ceil(h / 16);
            var level = (mbs <= 8192 && mbs * fps <= 245760) ? '28' : (mbs <= 8704 && mbs * fps <= 522240) ? '2A' : (mbs * fps <= 983040) ? '33' : '34';
            r.venc = new VideoEncoder({ output: add(r.vsrc), error: fail });
            r.venc.configure({ codec: 'avc1.6400' + level, width: w, height: h, framerate: fps,
                               bitrate: Math.round(w * h * fps * 0.1), alpha: 'discard', avc: { format: 'avc' } });
            r.aenc = new AudioEncoder({ output: add(r.asrc), error: fail });
            r.aenc.configure({ codec: 'mp4a.40.2', sampleRate: 48000, numberOfChannels: 2, bitrate: 192000 });
        } catch (e) {
            return false;
        }
        cur = r;
        return true;
    }

    // Called inside the rAF task before the swap: the drawing buffer still holds this frame.
    function video(index) {
        var r = cur;
        if (r.error) {
            return false;
        }
        var us = Math.round(index * 1e6 / r.fps);
        var f = new VideoFrame(r.canvas, { timestamp: us, duration: Math.round((index + 1) * 1e6 / r.fps) - us,
                                           visibleRect: { x: 0, y: 0, width: r.w, height: r.h } });
        r.venc.encode(f, { keyFrame: index % (r.fps * 2) === 0 });
        f.close();
        return true;
    }

    // stereo: a Float32Array view of wasm memory; AudioData copies it.
    function audio(stereo, frames) {
        var r = cur;
        if (r.error) {
            return false;
        }
        var a = new AudioData({ format: 'f32', sampleRate: 48000, numberOfFrames: frames, numberOfChannels: 2,
                                timestamp: Math.round(r.samples * 1e6 / 48000), data: stereo });
        r.samples += frames;
        r.aenc.encode(a);
        a.close();
        return true;
    }

    // Hands the recording to its own flush chain, so a new open never waits for it.
    function close(frames) {
        var r = cur;
        cur = null;
        var done = function (blob) {
            if (r.venc && r.venc.state !== 'closed') { r.venc.close(); }
            if (r.aenc && r.aenc.state !== 'closed') { r.aenc.close(); }
            var detail = { name: r.name, blob: blob, frames: frames, error: r.error || (frames ? null : 'no frames') };
            // Default action: download. A host that wants the blob (automation) cancels the event.
            if (window.dispatchEvent(new CustomEvent('nt-recorder', { detail: detail, cancelable: true })) && blob) {
                var a = document.createElement('a');
                a.href = URL.createObjectURL(blob);
                a.download = r.name;
                a.click();
                setTimeout(function () { URL.revokeObjectURL(a.href); }, 60000);
            }
        };
        if (!frames || r.error) {
            r.out.cancel().then(function () { done(null); }, function () { done(null); });
            return;
        }
        r.venc.flush()
            .then(function () { return r.aenc.flush(); })
            .then(function () { return r.chain; })
            .then(function () { return r.out.finalize(); })
            .then(function () { done(new Blob([r.out.target.buffer], { type: 'video/mp4' })); },
                  function (e) { if (!r.error) { r.error = String(e); } done(null); });
    }

    return { 'open': open, 'video': video, 'audio': audio, 'close': close };
})();
