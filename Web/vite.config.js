import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'

// In development the page comes from Vite (port 5173) while the chat server
// listens on 8081; proxy /ws so the client can always use its own origin.
// In production the C server serves the built files and /ws itself.
export default defineConfig({
  plugins: [react()],
  server: {
    proxy: {
      '/ws': {
        target: 'ws://127.0.0.1:8081',
        ws: true,
      },
    },
  },
})
