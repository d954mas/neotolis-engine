import { defineConfig } from 'astro/config';
import tailwindcss from '@tailwindcss/vite';

export default defineConfig({
  site: 'https://d954mas.github.io',
  base: '/neotolis-engine',
  vite: {
    plugins: [tailwindcss()],
  },
});
