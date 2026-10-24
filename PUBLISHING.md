# Publishing this fork

This folder is a clean source tree: no ROM, no `gen/`, no saves, no built
binary. It is not a git repository and it does not carry the upstream history,
so use it as the payload for a GitHub fork rather than creating a new
independent repository.

1. On GitHub, open https://github.com/ClickingMaster/ssrecomp and press
   **Fork**. (The parent is fixed when the fork is created; an existing
   standalone repo cannot be turned into a fork later.)
2. Clone your fork and create a branch:

       git clone https://github.com/TU_USUARIO/ssrecomp.git
       cd ssrecomp
       git switch -c tsp

3. Copy the contents of this folder over the clone (without deleting the
   clone's `.git`). For example, from the folder that contains this file:

       rsync -a --exclude='.git/' ./ /ruta/al/clon/ssrecomp/

4. Check what will be published. These must not appear:

       git status --short
       git ls-files | grep -Ei '\.(nds|sav|state|st|zip|7z)$|^gen/|^dist/|^build'

5. Commit and push only the branch. Do not force-push `main`; leave `main`
   equal to upstream so the fork stays clean:

       git add -A
       git commit -m "TrimUI Smart Pro / CrossMix port"
       git push -u origin tsp

The fork will show "forked from ClickingMaster/ssrecomp". In the README, keep
the note that the upstream project is no longer maintained and that the active
game source is `ClickingMaster/pokeheartgold-single-screen`.

For the build and deployment steps, see `port/README.md`. For licences and
credits, see `LICENSE` and `NOTICE`.
