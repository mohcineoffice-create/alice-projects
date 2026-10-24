# mcp-lqta

Read-only MCP server exposing public Lqta data, as stateless JSON-RPC on a Vercel function.

Tools: `lqta_lister_publications`, `lqta_voir_publication`, `lqta_rechercher`, `lqta_voir_profil`.

- Access requires a secret token in the URL, set as an environment variable on Vercel (nothing is stored in this repo).
- Inputs are sanitized against PostgREST filter injection.
- Tests: `node test.js`
