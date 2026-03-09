#!/usr/bin/env bash
set -euo pipefail

# ---------------------------------------------------------------------------
# init-gh-pages.sh -- Create or update the gh-pages branch with the size
# tracking chart page. Uses git worktree to avoid disturbing the working tree.
#
# Usage: bash scripts/init-gh-pages.sh
# ---------------------------------------------------------------------------

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

TMPDIR=$(mktemp -d)
trap "rm -rf '$TMPDIR'" EXIT

# ---------------------------------------------------------------------------
# Create chart page
# ---------------------------------------------------------------------------
cat > "$TMPDIR/index.html" << 'HTMLEOF'
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Neotolis Engine - Build Size Tracker</title>
<style>
  * { margin: 0; padding: 0; box-sizing: border-box; }
  body {
    font-family: system-ui, -apple-system, sans-serif;
    max-width: 960px;
    margin: 0 auto;
    padding: 24px 16px;
    color: #1a1a1a;
    background: #fff;
  }
  h1 { font-size: 1.5rem; margin-bottom: 8px; }
  .subtitle { color: #666; font-size: 0.875rem; margin-bottom: 24px; }
  .controls { display: flex; align-items: center; gap: 12px; margin-bottom: 16px; }
  .controls label { font-size: 0.875rem; font-weight: 600; }
  .controls select {
    padding: 4px 8px; font-size: 0.875rem;
    border: 1px solid #ccc; border-radius: 4px;
  }
  .summary {
    display: flex; gap: 24px; margin-bottom: 24px;
    padding: 16px; background: #f8f8f8; border-radius: 6px;
  }
  .summary-item { text-align: center; }
  .summary-item .label { font-size: 0.75rem; color: #666; text-transform: uppercase; }
  .summary-item .value { font-size: 1.25rem; font-weight: 700; margin-top: 4px; }
  .summary-item .sub { font-size: 0.75rem; color: #888; }
  .chart-container { position: relative; width: 100%; }
  .no-data {
    text-align: center; padding: 64px 16px; color: #999; font-size: 1rem;
  }
  .error { color: #c00; }
  canvas { width: 100% !important; }
</style>
</head>
<body>

<h1>Neotolis Engine - Build Size Tracker</h1>
<p class="subtitle">WASM release output sizes over time</p>

<div class="controls">
  <label for="target-select">Target:</label>
  <select id="target-select" onchange="loadTarget(this.value)">
    <option value="hello">hello</option>
  </select>
</div>

<div id="summary" class="summary" style="display:none"></div>

<div class="chart-container">
  <div id="no-data" class="no-data">Loading...</div>
  <canvas id="size-chart" style="display:none"></canvas>
</div>

<noscript>
  <p class="no-data">JavaScript is required to display the size history chart.</p>
</noscript>

<script src="https://cdn.jsdelivr.net/npm/chart.js@4/dist/chart.umd.min.js"></script>
<script>
(function () {
  'use strict';

  var chart = null;
  var noDataEl = document.getElementById('no-data');
  var canvasEl = document.getElementById('size-chart');
  var summaryEl = document.getElementById('summary');

  function formatBytes(bytes) {
    if (bytes < 1024) return bytes + ' B';
    return (bytes / 1024).toFixed(1) + ' KB';
  }

  function showNoData(msg) {
    noDataEl.textContent = msg;
    noDataEl.style.display = 'block';
    canvasEl.style.display = 'none';
    summaryEl.style.display = 'none';
  }

  function showChart() {
    noDataEl.style.display = 'none';
    canvasEl.style.display = 'block';
    summaryEl.style.display = 'flex';
  }

  function renderSummary(entry) {
    var html = '';
    html += '<div class="summary-item">';
    html += '<div class="label">Total Raw</div>';
    html += '<div class="value">' + formatBytes(entry.total.raw) + '</div>';
    html += '</div>';
    html += '<div class="summary-item">';
    html += '<div class="label">Total Gzip</div>';
    html += '<div class="value">' + formatBytes(entry.total.gzip) + '</div>';
    html += '</div>';
    var files = Object.keys(entry.files);
    for (var i = 0; i < files.length; i++) {
      var f = entry.files[files[i]];
      html += '<div class="summary-item">';
      html += '<div class="label">' + files[i] + '</div>';
      html += '<div class="value">' + formatBytes(f.raw) + '</div>';
      html += '<div class="sub">gzip: ' + formatBytes(f.gzip) + '</div>';
      html += '</div>';
    }
    summaryEl.innerHTML = html;
  }

  function renderChart(data) {
    var labels = data.map(function (e) { return e.commit; });
    var totalRaw = data.map(function (e) { return e.total.raw; });
    var totalGzip = data.map(function (e) { return e.total.gzip; });

    // Collect per-file datasets
    var fileNames = Object.keys(data[0].files);
    var fileColors = ['#e67e22', '#9b59b6', '#e74c3c', '#1abc9c', '#f1c40f'];
    var fileDatasets = [];
    for (var i = 0; i < fileNames.length; i++) {
      var name = fileNames[i];
      var color = fileColors[i % fileColors.length];
      fileDatasets.push({
        label: name + ' (raw)',
        data: data.map(function (e) { return e.files[name] ? e.files[name].raw : 0; }),
        borderColor: color,
        backgroundColor: color,
        borderWidth: 1,
        pointRadius: 2,
        hidden: true,
        tension: 0.1
      });
    }

    var config = {
      type: 'line',
      data: {
        labels: labels,
        datasets: [
          {
            label: 'Total Raw',
            data: totalRaw,
            borderColor: '#2563eb',
            backgroundColor: '#2563eb',
            borderWidth: 2,
            pointRadius: 3,
            tension: 0.1
          },
          {
            label: 'Total Gzip',
            data: totalGzip,
            borderColor: '#16a34a',
            backgroundColor: '#16a34a',
            borderWidth: 2,
            pointRadius: 3,
            tension: 0.1
          }
        ].concat(fileDatasets)
      },
      options: {
        responsive: true,
        interaction: { mode: 'index', intersect: false },
        scales: {
          x: {
            title: { display: true, text: 'Commit' }
          },
          y: {
            title: { display: true, text: 'Bytes' },
            beginAtZero: false,
            ticks: {
              callback: function (v) { return formatBytes(v); }
            }
          }
        },
        plugins: {
          tooltip: {
            callbacks: {
              label: function (ctx) {
                return ctx.dataset.label + ': ' + formatBytes(ctx.raw);
              }
            }
          }
        }
      }
    };

    if (chart) { chart.destroy(); }
    chart = new Chart(canvasEl.getContext('2d'), config);
  }

  window.loadTarget = function (target) {
    showNoData('Loading...');
    fetch('sizes/' + target + '.json')
      .then(function (res) {
        if (!res.ok) throw new Error('HTTP ' + res.status);
        return res.json();
      })
      .then(function (data) {
        if (!Array.isArray(data) || data.length === 0) {
          showNoData('No data yet for target "' + target + '".');
          return;
        }
        renderSummary(data[data.length - 1]);
        renderChart(data);
        showChart();
      })
      .catch(function () {
        showNoData('No data yet for target "' + target + '".');
      });
  };

  // Load default target on page load
  loadTarget('hello');
})();
</script>
</body>
</html>
HTMLEOF

mkdir -p "$TMPDIR/sizes"

# ---------------------------------------------------------------------------
# Push to gh-pages using a worktree to avoid disturbing current branch
# ---------------------------------------------------------------------------
cd "$ROOT_DIR"
BRANCH="gh-pages"
ORIGINAL_BRANCH=$(git symbolic-ref --short HEAD 2>/dev/null || git rev-parse --short HEAD)

if git show-ref --verify --quiet "refs/heads/$BRANCH"; then
    echo "gh-pages branch exists, updating..."
    git worktree add "$TMPDIR/worktree" "$BRANCH" 2>/dev/null
    cp "$TMPDIR/index.html" "$TMPDIR/worktree/"
    mkdir -p "$TMPDIR/worktree/sizes"
    cd "$TMPDIR/worktree"
    git add -A
    git diff --cached --quiet && echo "No changes to commit." || \
        git commit -m "update gh-pages: size tracking chart"
    cd "$ROOT_DIR"
    git worktree remove "$TMPDIR/worktree"
else
    echo "Creating gh-pages branch..."
    git worktree add --detach "$TMPDIR/worktree"
    cd "$TMPDIR/worktree"
    git checkout --orphan "$BRANCH"
    git rm -rf . 2>/dev/null || true
    cp "$TMPDIR/index.html" .
    mkdir -p sizes
    git add -A
    git commit -m "init gh-pages: size tracking chart"
    cd "$ROOT_DIR"
    git worktree remove "$TMPDIR/worktree"
fi

echo ""
echo "gh-pages branch ready. Push with:"
echo "  git push origin gh-pages"
