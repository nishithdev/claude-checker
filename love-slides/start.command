#!/bin/bash
# Double-click this file on macOS to launch the Love Slides flasher.
# It starts a local web server and opens the flasher in Chrome.

cd "$(dirname "$0")/../docs"

# Kill any existing server on port 8080
lsof -ti:8080 | xargs kill -9 2>/dev/null || true

# Start server in background
python3 -m http.server 8080 &
SERVER_PID=$!

sleep 0.8

# Open in Chrome (Web Serial requires Chrome or Edge)
if open -a "Google Chrome" http://localhost:8080/ 2>/dev/null; then
  echo "Opened in Chrome"
elif open -a "Microsoft Edge" http://localhost:8080/ 2>/dev/null; then
  echo "Opened in Edge"
else
  open http://localhost:8080/
fi

echo "Server running at http://localhost:8080/"
echo "Press Ctrl+C to stop."

wait $SERVER_PID
