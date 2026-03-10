import { defineCollection, z } from 'astro:content';
import { glob } from 'astro/loaders';

const examples = defineCollection({
  loader: glob({
    pattern: '**/site_promo/index.md',
    base: '../examples',
    generateId: ({ entry }) => {
      // entry is like "hello/site_promo/index.md"
      // Extract example name from path
      return entry.split('/')[0];
    },
  }),
  schema: z.object({
    title: z.string(),
    description: z.string(),
  }),
});

export const collections = { examples };
