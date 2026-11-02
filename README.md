# my-malloc — un `malloc` minimal, expliqué

> Projet pédagogique : reproduire et durcir l'implémentation d'allocateur décrite dans
> **« Malloc is not magic — Implementing my own memory allocator »** d'Alejandro Nadal
> (Level Up Coding, avril 2025).
>
> Article : <https://levelup.gitconnected.com/malloc-is-not-magic-implementing-my-own-memory-allocator-e0354e914402>

Ce dépôt est une **réécriture indépendante**, pas une copie du code de l'article. Il ajoute
quelques garde-fous supplémentaires (alignement garanti, rejet du double free, initialisation
concurrente, restitution des pages à l'OS) et une couche système portable Linux/WSL, macOS et
Windows.

> ⚠️ **Ce n'est pas un remplacement de `malloc`.** Pas de `realloc`, pas de `calloc`, pas de
> durcissement mémoire, pas de support des très gros objets, et la recherche de bloc est
> linéaire. Ne l'utilisez pas en production ni via `LD_PRELOAD`.

---

## Sommaire

- [Ce que fait le projet](#ce-que-fait-le-projet)
- [Arborescence](#arborescence)
- [Compiler](#compiler)
- [Exécuter](#exécuter)
- [Exemple de sortie](#exemple-de-sortie)
- [Comment ça marche](#comment-ça-marche)
- [API](#api)
- [Tests](#tests)
- [Écarts assumés par rapport à l'article](#écarts-assumés-par-rapport-à-larticle)
- [Limites connues](#limites-connues)
- [Pistes d'exercices](#pistes-dexercices)
- [Dépannage](#dépannage)
- [Licence](#licence)

---

## Ce que fait le projet

Un allocateur de tas complet et lisible, construit autour d'une liste doublement chaînée :

- en-têtes de bloc de 32 octets (`magic`, `in_use`, `length`, `prev`, `next`) ;
- recherche **best fit** (le plus petit bloc libre assez grand) ;
- découpage (*split*) du bloc choisi, avec seuil minimal pour éviter les miettes ;
- fusion (*coalescing*) avec le bloc suivant puis le précédent à chaque `free` ;
- croissance du tas page par page, et **restitution des pages de queue à l'OS** ;
- charge utile alignée sur 16 octets (compatible `max_align_t`) ;
- rejet des pointeurs invalides et des doubles libérations (plage + `magic` + état) ;
- initialisation unique et sûre entre threads (C11 atomics) ;
- statistiques, dump du tas et vérification d'invariants ;
- backend système abstrait : `mmap`/`madvise` (POSIX), `VirtualAlloc` (Windows), `sbrk` en option.

L'allocateur et sa couche système tiennent en un peu plus de 500 lignes de C11, sans
dépendance externe.

---

## Arborescence

```text
my-malloc/
├── CMakeLists.txt
├── LICENSE
├── README.md
├── .gitignore
├── .github/
│   └── workflows/
│       └── ci.yml          # compilation + tests sous Linux et Windows
├── include/
│   └── mymalloc.h          # API publique
├── src/
│   ├── mm_os.h             # interface avec l'OS
│   ├── mm_os.c             # backends VirtualAlloc / mmap / sbrk
│   └── mymalloc.c          # l'allocateur
├── apps/
│   └── demo.c              # démonstration pas à pas
└── tests/
    └── test_mymalloc.c     # 9 tests, chacun isolé dans son processus
```

---

## Compiler

Prérequis : un compilateur C11, CMake ≥ 3.16, et un générateur (`make`, Ninja ou Visual Studio).

### Linux, WSL, macOS

```bash
cmake -S . -B build
cmake --build build
```

### Windows avec MSYS2 (UCRT64)

Dans un terminal **MSYS2 UCRT64** :

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-gcc \
                   mingw-w64-ucrt-x86_64-cmake \
                   mingw-w64-ucrt-x86_64-ninja
cmake -S . -B build -G Ninja
cmake --build build
```

Si vous lancez CMake depuis PowerShell, ajoutez d'abord le compilateur au `PATH` :

```powershell
$env:PATH = "C:\msys64\ucrt64\bin;" + $env:PATH
cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER="C:/msys64/ucrt64/bin/gcc.exe"
cmake --build build
```

### Windows avec Visual Studio 2022

Le projet compile aussi avec MSVC (testé avec Visual Studio 2022 17.14, MSVC 19.44) :

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

`CMakeLists.txt` ajoute `/std:c11 /experimental:c11atomics` pour MSVC : sans ce second
flag, la bibliothèque standard refuse `<stdatomic.h>`.

### Options CMake

| Option | Défaut | Effet |
|---|---|---|
| `MYMALLOC_BUILD_TESTS` | `ON` | Compile la suite de tests et l'enregistre dans CTest. |
| `MYMALLOC_USE_SBRK` | `OFF` | Utilise `sbrk()` comme dans l'article. **POSIX uniquement.** |

Exemple :

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake -S . -B build-sbrk -DMYMALLOC_USE_SBRK=ON   # Linux/WSL/macOS
```

> `MYMALLOC_USE_SBRK=ON` est refusé par CMake sous Windows : le backend `sbrk` n'existe pas
> et `VirtualAlloc` est utilisé à la place.

---

## Exécuter

```bash
./build/demo            # démonstration commentée
./build/test_mymalloc   # suite complète
ctest --test-dir build --output-on-failure
```

Sous Windows, ajoutez `.exe` : `.\build\demo.exe`, `.\build\test_mymalloc.exe`.

Un test seul (le nom exact est celui affiché par la suite de tests) :

```bash
./build/test_mymalloc stress
```

---

## Exemple de sortie

Extrait de `demo` (sortie volontairement en ASCII, pour éviter les problèmes de code page
dans les consoles Windows) :

```text
[3 allocations : 64 / 128 / 256 octets]
  blocs=4  pages=1  tas=4096 o  alloue=448 o  libre=3488 o  plus_grand_libre=3488 o
  #    offset     state    bytes
  0    32         used     64
  1    128        used     128
  2    288        used     256
  3    576        free     3488

[apres free(a) et free(c)]
  blocs=1  pages=1  tas=4096 o  alloue=0 o  libre=4032 o  plus_grand_libre=4032 o
  #    offset     state    bytes
  0    32         free     4032

[apres free(big)]
  blocs=1  pages=1  tas=4096 o  alloue=0 o  libre=4032 o  plus_grand_libre=4032 o
  #    offset     state    bytes
  0    32         free     4032

--- les pieges ---
free(x)        : accepte (normal)
double free    : refuse correctement
free invalide  : refuse correctement
free(NULL)     : accepte (normal)

integrite du tas : OK
```

---

## Comment ça marche

### Disposition du tas

```text
g_heap ──► ┌────────────────────────────┐ offset 0
           │ en-tête du tas (32 octets) │  magic, nb_blocs, nb_pages, taille
           ├────────────────────────────┤ offset 32
           │ en-tête de bloc (32 octets)│  magic, in_use, length, prev, next
           │ charge utile (length)      │  ← pointeur rendu à l'appelant
           ├────────────────────────────┤
           │ en-tête de bloc (32 octets)│
           │ charge utile               │
           ├────────────────────────────┤
           │            ...             │
           └────────────────────────────┘ offset heap_size
```

Les blocs sont **contigus** : le bloc suivant commence immédiatement après la charge utile du
précédent. C'est cet invariant qui rend `mm_check()` possible et qui simplifie la fusion.

### `my_malloc(size)`

1. Initialise le tas une seule fois (CAS atomique, les autres threads attendent).
2. Arrondit `size` à 16 octets ; refuse `0` et les débordements.
3. Cherche le plus petit bloc libre suffisant (best fit).
4. S'il n'y en a pas, commite des pages supplémentaires en fin de tas. Si le dernier bloc est
   libre, il est simplement agrandi ; sinon un nouveau bloc libre est ajouté après lui.
5. Découpe le bloc si le reste peut contenir un en-tête plus 16 octets utiles.
6. Marque le bloc utilisé et renvoie `bloc + 32`.

### `my_free(ptr)`

1. Vérifie que `ptr` tombe dans la plage du tas, que l'en-tête précédent porte le bon `magic`
   et que le bloc est bien marqué utilisé. Sinon, renvoie `false` (pointeur étranger ou double
   free).
2. Fusionne avec le bloc suivant s'il est libre.
3. Fusionne avec le bloc précédent s'il est libre.
4. Empoisonne les en-têtes absorbés (`magic = 0`).
5. Rogne la queue libre et rend à l'OS toutes les pages entières devenues inutiles.

### Invariants vérifiés par `mm_check()`

- la liste parcourt le tas sans trou ni chevauchement ;
- `prev` et `next` sont cohérents dans les deux sens ;
- chaque en-tête porte `MM_BLOCK_MAGIC` ;
- chaque taille de charge utile est multiple de 16 ;
- deux blocs libres ne sont jamais adjacents (sinon la fusion a été oubliée) ;
- la liste se termine exactement sur `heap_size`.

### Backends système

| Plateforme | Réservation | Commit | Restitution |
|---|---|---|---|
| Windows | `VirtualAlloc(MEM_RESERVE)` | `VirtualAlloc(MEM_COMMIT)` | `VirtualFree(MEM_DECOMMIT)` |
| POSIX (défaut) | `mmap(MAP_NORESERVE)` | à la première écriture | `madvise(MADV_DONTNEED)` |
| POSIX + `-DMYMALLOC_USE_SBRK=ON` | `sbrk(0)` | `sbrk(+n)` | `sbrk(-n)` |

Le backend `mmap` est le défaut POSIX, et c'est volontaire : la libc utilise elle aussi
`sbrk()` pour son propre tas. Déplacer le *program break* dans son dos corrompt sa vision du
tas, et le programme peut mourir au premier `printf`. `mmap` réserve une arène privée que la
libc ne touche jamais. Le mode `sbrk` reste disponible pour observer le conflit avec la libc,
mais il n'est pas sûr dès que celle-ci alloue entre deux de nos appels.

---

## API

Déclarations dans [`include/mymalloc.h`](include/mymalloc.h).

| Fonction | Rôle |
|---|---|
| `void mm_init(void)` | Initialise le tas. Appelée automatiquement par `my_malloc`. |
| `void *my_malloc(size_t size)` | Alloue au moins `size` octets. `NULL` si `size == 0` ou en cas d'échec. |
| `bool my_free(void *ptr)` | Libère un bloc. `NULL` est accepté. `false` si le pointeur est invalide ou déjà libéré. |
| `size_t my_usable_size(void *ptr)` | Taille utile d'une allocation vivante, `0` si le pointeur est invalide. |
| `void mm_get_stats(mm_stats *out)` | Remplit un instantané des statistiques. |
| `bool mm_check(void)` | Vérifie les invariants du tas. |
| `void mm_dump(FILE *out)` | Affiche la liste des blocs sous forme de tableau. |

`mm_stats` expose `block_count`, `page_count`, `heap_size`, `allocated_bytes`, `free_bytes` et
`largest_free`.

---

## Tests

`tests/test_mymalloc.c` contient neuf tests :

| Test | Ce qu'il vérifie |
|---|---|
| `init` | État initial : un bloc libre, une page, invariants OK. |
| `basic` | Allocation, écriture/lecture, taille utile. |
| `alignment` | Tout pointeur rendu est aligné sur 16 octets. |
| `grow` | Une demande de 5000 octets fait grandir le tas et les données survivent. |
| `coalesce` | Trois blocs libérés dans le désordre redeviennent un seul bloc. |
| `best_fit` | Un petit bloc réutilise le trou de 1000 octets, pas la grande queue libre. |
| `invalid_free` | Un pointeur de pile est refusé ; `free(NULL)` est accepté. |
| `double_free` | La seconde libération est refusée. |
| `stress` | 20 000 allocations/libérations pseudo-aléatoires avec vérification des contenus et des invariants. |

Chaque test s'exécute **dans son propre processus** : `fork()` sous POSIX, ré-exécution de
l'exécutable avec `_spawnv` sous Windows. Un `abort()` ou un crash ne tue donc que le test
concerné, et la suite continue.

Une CI GitHub Actions (`.github/workflows/ci.yml`) compile et teste sous Linux et sous
Windows/MSYS2, et vérifie que le backend `sbrk` compile sous Linux.

---

## Écarts assumés par rapport à l'article

| Sujet | L'article | Ce dépôt |
|---|---|---|
| Backend | `sbrk()` | `mmap`/`madvise` par défaut, `sbrk` en option |
| Alignement | non garanti | 16 octets garantis (`max_align_t`) |
| Recherche | premier bloc libre assez grand | best fit (plus petit bloc suffisant) |
| Seuil de découpage | aucun | pas de split s'il reste moins d'un en-tête + 16 octets |
| Fusion | avec le voisin suivant | suivants **et** précédents, dans les deux sens |
| Pages rendues à l'OS | non | queue libre rognée + pages décommitées |
| Initialisation concurrente | course possible | CAS atomique + attente des autres threads |
| Double free | comportement indéfini | refusé (`magic` + `in_use`) |
| `free` d'un pointeur étranger | comportement indéfini | refusé (test de plage + `magic`) |
| Introspection | aucune | `mm_stats`, `mm_dump`, `mm_check` |
| Portabilité | Linux | Linux, WSL, macOS, Windows |

L'article est un article pédagogique : ces écarts ne sont pas des reproches, mais les choix
qu'il faut faire pour passer d'un exemple lisible à un allocateur utilisable au quotidien.

Quelques précisions :

- **Alignement.** `malloc` doit pouvoir stocker n'importe quel type, donc renvoyer un pointeur
  aligné sur `_Alignof(max_align_t)` (16 sur x86-64 et arm64). Les tailles sont arrondies à 16,
  ce qui garde tous les blocs suivants alignés.
- **Seuil de découpage.** Découper un reste plus petit que `sizeof(mm_block) + 16` créerait un
  bloc impossible à réutiliser et impossible à fusionner proprement. On préfère laisser le
  bloc entier au demandeur.
- **Restitution des pages.** Sans elle, un programme qui alloue puis libère un gros buffer garde
  les pages pour toujours. `mm_shrink_tail()` tronque le bloc libre de queue à la plus petite
  empreinte entière en pages, puis décommit ce qui dépasse.
- **Détection des erreurs.** Le `magic` et le test de plage sont des aides au débogage, **pas**
  une protection contre un attaquant : un pointeur forgé — ou un pointeur intérieur tombant
  par hasard sur un motif valide — peut passer. Un vrai durcissement demanderait des canaris,
  des guard pages et une vérification systématique des métadonnées.
- **Threads.** Un verrou tournant global protège `my_malloc` et `my_free`. C'est correct mais
  peu scalable. `my_usable_size`, `mm_dump` et `mm_check` lisent sans verrou : à n'appeler que
  lorsque le tas est au repos.

---

## Limites connues

- **Pas de `calloc`, `realloc`, `aligned_alloc` ni `memalign`.**
- **Recherche linéaire O(n)** : le coût d'allocation croît avec le nombre de blocs.
- **Pas de classes de taille** ni de listes libres séparées : adapté à l'apprentissage, pas à
  une charge réelle.
- **Verrou global** : pas de tas par thread, pas de scalabilité multicœur.
- **Pas de durcissement mémoire** : pas de canaris, pas de guard pages, pas d'ASLR dédiée.
- **Pas de gros objets dédiés** : pas de `mmap` par allocation au-delà d'un seuil.
- **Réserve d'adresse fixe de 256 Mo** (`MM_HEAP_RESERVE`) : à ajuster sur les systèmes
  contraints ou 32 bits.
- **Taille de page codée en dur à 4096 octets** : correct sur x86-64 et arm64, à rendre
  configurable ailleurs.
- **Le mode `sbrk` n'est pas sûr** dès que la libc alloue entre deux appels : c'est justement
  le piège que l'article illustre.
- **Pas un remplacement de la libc** : ne pas utiliser via `LD_PRELOAD`, `malloc` interposé ou
  dans du code de production.

---

## Pistes d'exercices

1. Implémenter `my_calloc` (attention au débordement `nmemb * size`).
2. Implémenter `my_realloc` (agrandir sur place si le bloc suivant est libre, sinon
   allouer/copier/libérer).
3. Ajouter des listes libres séparées par classes de taille.
4. Allouer les très gros blocs directement avec `mmap`/`VirtualAlloc`, hors du tas.
5. Ajouter des canaris autour des charges utiles et des guard pages pour détecter les
   débordements.
6. Donner un arène par thread (`_Thread_local`) pour supprimer le verrou global.
7. Comparer les performances à la libc avec un petit benchmark (même nombre d'allocations,
   tailles mélangées).
8. Compiler avec `-DMYMALLOC_USE_SBRK=ON` sous Linux et provoquer la corruption avec des
   allocations libc intercalées, puis expliquer précisément ce qui casse.

---

## Dépannage

- **MSYS2 : `cc1` échoue sans message, code de sortie 1.** Le dossier `C:\msys64\ucrt64\bin`
  n'est pas dans le `PATH`. Ajoutez-le avant de lancer CMake.
- **CMake/GCC mangent les chemins (`d├®faut`, `dÚfaut`).** Évitez les caractères non ASCII
  dans le chemin du projet. Un chemin comme `C:\dev\my-malloc` fonctionne toujours.
- **`MYMALLOC_USE_SBRK=ON` échoue sous Windows.** C'est normal : l'option est réservée à
  POSIX et CMake s'arrête avec un message explicite.
- **Le test `stress` est lent en Debug.** Il fait 20 000 opérations avec vérification
  périodique des invariants. Compilez en `Release` pour le benchmark, gardez `Debug` pour
  déboguer.

---

## Licence

MIT — voir [`LICENSE`](LICENSE).

Le texte et le code de l'article original appartiennent à leur auteur, Alejandro Nadal. Ce
dépôt est une réimplémentation pédagogique indépendante, écrite d'après la description de
l'algorithme. Le projet n'est pas affilié à l'auteur ni à Level Up Coding.
