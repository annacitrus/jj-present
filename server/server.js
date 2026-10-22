const express = require('express');
const path = require('path');

const WIDTH = 320;
const HEIGHT = 240;
const IMAGE_BYTES = WIDTH * HEIGHT * 2; // RGB565, big-endian

const app = express();

// In-memory store for the latest frame. Starts all-black.
let latestImage = Buffer.alloc(IMAGE_BYTES, 0);
let version = 0;

app.use(express.static(path.join(__dirname, 'public')));
app.use(express.raw({ type: 'application/octet-stream', limit: '1mb' }));

app.get('/api/version', (req, res) => {
  res.json({ version, width: WIDTH, height: HEIGHT });
});

app.get('/api/image', (req, res) => {
  res.set('Content-Type', 'application/octet-stream');
  res.set('X-Version', String(version));
  res.send(latestImage);
});

app.post('/api/image', (req, res) => {
  if (!Buffer.isBuffer(req.body) || req.body.length !== IMAGE_BYTES) {
    res.status(400).json({
      error: `expected ${IMAGE_BYTES} bytes (${WIDTH}x${HEIGHT} RGB565), got ${req.body ? req.body.length : 0}`,
    });
    return;
  }
  latestImage = req.body;
  version += 1;
  res.json({ ok: true, version });
});

const PORT = process.env.PORT || 3000;
app.listen(PORT, () => {
  console.log(`Relay server listening on port ${PORT}`);
});
