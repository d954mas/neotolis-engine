// nt_recorder web backend: WebCodecs H.264 + AAC (Opus where the browser has no AAC encoder) from the
// canvas, muxed to MP4 by Mediabunny. Loaded with --extern-pre-js after deps/mediabunny/nt_mediabunny.js,
// outside Closure, so the WebCodecs and Mediabunny property names stay intact in wasm-release.
globalThis['NtRecorder'] = (function () {
    var cur = null;
    var url = null; // the last download's blob URL, revoked on the next (Chrome may ask before it)
    // Probed once at load (Firefox and Linux Chrome lack AAC, a software-only Chrome lacks H.264 High);
    // open refuses until the probe settles, so a start never picks a codec the browser rejects.
    var profile = null;
    var acodec = null;
    var probed = typeof VideoEncoder === 'undefined' || typeof AudioEncoder === 'undefined';
    if (!probed) {
        var supported = function (enc, cfg) {
            return enc.isConfigSupported(cfg).then(function (s) { return s.supported; }, function () { return false; });
        };
        // A small config: a bigger canvas the encoder rejects ends that recording at its first frame.
        var vcfg = { width: 640, height: 360, framerate: 30 };
        var acfg = { sampleRate: 48000, numberOfChannels: 2 };
        Promise.all([
            supported(VideoEncoder, Object.assign({ codec: 'avc1.64001F' }, vcfg)).then(function (high) {
                return high || supported(VideoEncoder, Object.assign({ codec: 'avc1.42E01F' }, vcfg))
                    .then(function (base) { profile = base ? '42E0' : null; return high; });
            }).then(function (high) { if (high) { profile = '6400'; } }),
            supported(AudioEncoder, Object.assign({ codec: 'mp4a.40.2' }, acfg)).then(function (aac) {
                return aac || supported(AudioEncoder, Object.assign({ codec: 'opus' }, acfg))
                    .then(function (opus) { acodec = opus ? 'opus' : null; return aac; });
            }).then(function (aac) { if (aac) { acodec = 'mp4a.40.2'; } }),
        ]).then(function () { probed = true; });
    }
    // H.264 levels: [level_idc hex, max frame macroblocks, max macroblocks per second].
    var LEVELS = [['1F', 3600, 108000], ['28', 8192, 245760], ['2A', 8704, 522240], ['32', 22080, 589824], ['33', 36864, 983040],
                  ['34', 36864, 2073600], ['3C', 139264, 4177920], ['3E', 139264, 16711680]];

    // The browser may already have closed an encoder (resource reclamation).
    function closeEncoders(r) {
        if (r.venc && r.venc.state !== 'closed') { r.venc.close(); }
        if (r.aenc && r.aenc.state !== 'closed') { r.aenc.close(); }
    }

    function release(r) {
        closeEncoders(r);
        if (r.out) { r.out.cancel().catch(function () {}); }
    }

    function open(name, canvas, w, h, fps) {
        if (!probed || !profile || !acodec) {
            if (!probed) { console.warn('nt_recorder: codec probe still running, start again'); }
            return false;
        }
        var mbs = Math.ceil(w / 16) * Math.ceil(h / 16);
        var level = LEVELS.find(function (l) { return mbs <= l[1] && mbs * fps <= l[2]; });
        if (!level) {
            console.warn('nt_recorder: ' + w + 'x' + h + ' at ' + fps + ' fps exceeds H.264 level 6.2');
            return false;
        }
        var MB = NtMediabunny;
        var r = { name: name, canvas: canvas, w: w, h: h, fps: fps, error: null, samples: 0 };
        var fail = r.fail = function (e) { if (!r.error) { r.error = String(e); } };
        try {
            r.out = new MB.Output({ format: new MB.Mp4OutputFormat(), target: new MB.BufferTarget() });
            r.vsrc = new MB.EncodedVideoPacketSource('avc');
            r.asrc = new MB.EncodedAudioPacketSource(acodec === 'opus' ? 'opus' : 'aac');
            r.out.addVideoTrack(r.vsrc, { frameRate: fps });
            r.out.addAudioTrack(r.asrc);
            // Packets join the muxer strictly in output order through one promise chain.
            r.chain = r.out.start();
            r.chain.catch(fail);
            // The muxer reads the decoder config from the first packet's metadata and ignores the rest.
            var add = function (src) {
                return function (chunk, meta) {
                    var p = MB.EncodedPacket.fromEncodedChunk(chunk);
                    r.chain = r.chain.then(function () { return src.add(p, meta); });
                    r.chain.catch(fail);
                };
            };
            r.venc = new VideoEncoder({ output: add(r.vsrc), error: fail });
            r.venc.configure({ codec: 'avc1.' + profile + level[0], width: w, height: h, framerate: fps,
                               bitrate: Math.round(w * h * fps * 0.1), alpha: 'discard', avc: { format: 'avc' } });
            r.aenc = new AudioEncoder({ output: add(r.asrc), error: fail });
            r.aenc.configure({ codec: acodec, sampleRate: 48000, numberOfChannels: 2, bitrate: 192000 });
        } catch (e) {
            release(r);
            return false;
        }
        cur = r;
        return true;
    }

    // Called inside the rAF task before the swap: the drawing buffer still holds this frame. A throw
    // (lost context) ends the recording instead of unwinding into wasm.
    function video(index) {
        var r = cur;
        // Nothing paces the game to the encoder: two seconds of backlog beyond one call's catch-up
        // (at most two seconds of frames) end the recording.
        if (!r.error && r.venc.encodeQueueSize > r.fps * 4) { r.fail('encoder backlog'); }
        if (r.error) {
            return false;
        }
        var us = Math.round(index * 1e6 / r.fps);
        try {
            var f = new VideoFrame(r.canvas, { timestamp: us, duration: Math.round((index + 1) * 1e6 / r.fps) - us,
                                               visibleRect: { x: 0, y: 0, width: r.w, height: r.h } });
            try { r.venc.encode(f, { keyFrame: index % (r.fps * 2) === 0 }); } finally { f.close(); }
        } catch (e) {
            r.fail(e);
            return false;
        }
        return true;
    }

    // stereo: a Float32Array view of wasm memory; AudioData copies it.
    function audio(stereo, frames) {
        var r = cur;
        if (r.error) {
            return false;
        }
        try {
            var a = new AudioData({ format: 'f32', sampleRate: 48000, numberOfFrames: frames, numberOfChannels: 2,
                                    timestamp: Math.round(r.samples * 1e6 / 48000), data: stereo });
            try { r.aenc.encode(a); } finally { a.close(); }
        } catch (e) {
            r.fail(e);
            return false;
        }
        r.samples += frames;
        return true;
    }

    // Hands the recording to its own flush chain, so a new open never waits for it. The event fires
    // after the C call returns, never inside it.
    function close(frames) {
        var r = cur;
        cur = null;
        var done = function (blob) {
            if (!blob) {
                release(r);
            }
            var detail = { name: r.name, blob: blob, frames: frames, error: r.error || (frames ? null : 'no frames') };
            // Default action: download. A host that wants the blob (automation) cancels the event.
            if (window.dispatchEvent(new CustomEvent('nt-recorder', { detail: detail, cancelable: true })) && blob) {
                if (url) { URL.revokeObjectURL(url); }
                var a = document.createElement('a');
                a.href = url = URL.createObjectURL(blob);
                a.download = r.name;
                a.click();
            }
        };
        if (!frames || r.error) {
            Promise.resolve().then(function () { done(null); });
            return;
        }
        r.venc.flush()
            .then(function () { return r.aenc.flush(); })
            .then(function () { return r.chain; })
            .then(function () { return r.out.finalize(); })
            .then(function () {
                if (r.error) { throw r.error; }
                closeEncoders(r);
                return new Blob([r.out.target.buffer], { type: 'video/mp4' });
            })
            .catch(function (e) { r.fail(e); return null; })
            .then(done);
    }

    return { 'open': open, 'video': video, 'audio': audio, 'close': close };
})();
