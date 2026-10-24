# Lqta

A photo, short video, stories and private messages app, written as a single-file vanilla JavaScript front end on Supabase.

- **Auth, Postgres, Storage** on Supabase; access control is enforced by row-level security policies, not by the client.
- Posts (photo or video up to 30 s / 20 MB), Reels, stories, likes, comments, follows and direct messages.
- Live: [lqta.vercel.app](https://lqta.vercel.app)

The Supabase *publishable* key inside `index.html` is public by design.
