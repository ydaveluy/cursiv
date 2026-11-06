# Passation autonome — audit de maintenabilité de Cursiv

Campagne du 6 novembre 2026 — **513 périmètres audités ; 158 constats uniques : 118 établis et 40 candidats**.

Dépôt : [ydaveluy/cursiv](https://github.com/ydaveluy/cursiv). Révision analysée : [`66c465bba3d2abd655e0e8fad3063f1fcfa9d599`](https://github.com/ydaveluy/cursiv/tree/66c465bba3d2abd655e0e8fad3063f1fcfa9d599). Tous les liens de preuve pointent vers cette révision immuable. Aucune modification du code source n’a été faite ; la révision analysée reste distincte du commit de publication documentaire.

L’objectif est de repérer le code mort, dupliqué, factorisable, legacy, simplifiable ou optimisable. Le catalogue de 513 périmètres de l’audit précédent a été conservé, mais les sources et usages ont été relus pour cette campagne. Les questions historiques de comparaison à Langium ne sont pas transformées artificiellement en défauts de maintenance : une fonctionnalité absente n’est pas du code mort. Les anciens rapports restent distincts.

**Méthode : lecture statique uniquement. Aucun reproducer, build, test, benchmark ni exécution du projet.** Les seuls scripts exécutés pour ce livrable lisent des textes, comparent des fichiers et assemblent/contrôlent la documentation. Les validations proposées dans les fiches sont des travaux futurs, jamais des résultats obtenus ici. Aucune accélération mesurée n’est annoncée.

Ce document est autonome pour investiguer tous les constats et préparer les correctifs : il contient pour chacun les preuves, usages, conditions, changements minimaux et invariants. Le rapport exhaustif ajoute les 513 fiches, y compris les exclusions sans constat.

Cette campagne est publiée sur la branche `audit`, dans `audits/2026-11-06/maintenance/`. Les [rapports de la campagne précédente](../SYNTHESE-PASSATION.md) sont conservés dans le dossier parent.

## Lire les niveaux de preuve

- **Établi** : la structure décrite est directement vérifiable dans les sources. Cela ne certifie pas qu’un correctif proposé est sans risque, ni que son gain sera important.
- **Candidat** : opportunité conditionnelle, dont la pertinence, les invariants ou l’impact doivent encore être confirmés.
- **P2** : opportunité concrète de complexité, allocation, contention ou maintenance. **P3** : amélioration locale ou conditionnelle. Aucun P1 n’a été attribué à cette campagne ; ces niveaux ne sont pas des sévérités de bugs.
- **Code mort** : absence de consommation établie pour une donnée ou branche précisément délimitée. Un symbole public, template, hook, fonction de plateforme ou fragment de C++ émis en chaîne ne devient jamais mort par la seule absence d’appel trouvé dans le dépôt.
- **Legacy** : trace obsolète identifiable, généralement un commentaire ou chemin qui ne correspond plus à l’implémentation. Les adaptateurs de compatibilité actifs restent à conserver.

Les chemins des fiches sont relatifs à la racine du dépôt. Les numéros de ligne sont ceux de la révision ci-dessus. `condition`, `coût` et `préserver` doivent être lus ensemble : plusieurs pistes peuvent être des régressions sur les petits cas si l’on ajoute un index ou cache systématique.

## Bilan chiffré

| Catégorie | Constats uniques |
| --- | --- |
| mort | 3 |
| duplication | 5 |
| factorisation | 14 |
| legacy | 5 |
| simplification | 11 |
| optimisation | 120 |

| Preuve / priorité / couverture | Nombre |
| --- | --- |
| Établis | 118 |
| Candidats | 40 |
| P2 | 104 |
| P3 | 54 |
| Périmètres avec constat ou référence | 229 |
| Périmètres sans constat retenu | 284 |
| Périmètres avec réserve seule | 0 |
| Périmètres non audités | 0 |

Les alias historiques **BLD-021 → ASS-020** et **BLD-022 → TXT-003** ne sont pas comptés une seconde fois. Les 513 identifiants uniques correspondent exactement au catalogue. La vérification documentaire confirme les identifiants, les renvois et l’existence des fichiers/lignes ; elle ne remplace pas une preuve de correction des algorithmes.

[Rapport exhaustif et ses 513 fiches](AUDIT-MAINTENABILITE-513.md).

## Conclusions transversales et frontières des correctifs

**Suppression locale.** Trois cas sont suffisamment isolés pour proposer un diff étroit : [M-LEX-PARTITION-QUEUED](#finding-m-lex-partition-queued), [M-LR-MULTI-UNUSED-UNION](#finding-m-lr-multi-unused-union) et [M-PUB-JSON-FINITE-GUARD](#finding-m-pub-json-finite-guard). Le premier est un vecteur local uniquement écrit ; le deuxième une union de sets calculée puis abandonnée ; le troisième une garde privée rendue redondante par la normalisation du constructeur. Ne supprimer ni les fonctions qui les entourent, ni les autres variables homonymes, ni le traitement public NaN/inf. Deux relectures indépendantes ont confirmé ces limites.

**LL : génération, recherche et mémoire de session.** Les gains candidats portent sur les balayages répétés des graphes et ensembles, les admissions dans les frontiers, le travail d’entrée et le stockage des plans. Points d’entrée : [M-LL-SHAPE-GRAPH](#finding-m-ll-shape-graph), [M-LL-PLAIN-ENTRY](#finding-m-ll-plain-entry), [M-LL-CELL-MEMBERSHIP](#finding-m-ll-cell-membership), [M-LL-FRONTIER-INSERT](#finding-m-ll-frontier-insert) et [M-LL-MEMO-UNREACHABLE-TAIL](#finding-m-ll-memo-unreachable-tail). Ce dernier démontre une rétention : `find_at` ne consulte que 16 lignes récentes par clé, alors que lignes et plans restent append-only. Mais **une copie de `LlRemembered` peut encore être passée à `lay`** : une ancienne ligne hors horizon de recherche n’est pas automatiquement supprimable. Toute compaction doit préserver ce contrat ou définir explicitement son évolution, ainsi que indices, ancienneté et dépendance EOF. [M-LL-TLS-ROOM](#finding-m-ll-tls-room) traite une autre durée de vie, celle des capacités gardées par thread.

**LR : coût de construction, graphes et représentation des candidats.** Les scans par paires, la recopie des ensembles et chemins et les états temporaires offrent davantage de pistes que la suppression de grandes fonctions. Consulter [M-LR-LOWER-DEPTH](#finding-m-lr-lower-depth), [M-LR-UNIT-EARLY-SNAPSHOT](#finding-m-lr-unit-early-snapshot), [M-LR-ELECT-TWIN-POOL](#finding-m-lr-elect-twin-pool), [M-LR-GSS-CHUNKS-REUSE](#finding-m-lr-gss-chunks-reuse), [M-LR-GSS-ACCEPT-PATHS](#finding-m-lr-gss-accept-paths) et [M-LR-CACTUS-SIBLINGS](#finding-m-lr-cactus-siblings). Le fait que deux chemins utilisent le même prédicat lexical ne prouve pas qu’ils sont factorisables : dans [M-LR-LEXICAL-SHIFT-PAIRS](#finding-m-lr-lexical-shift-pairs), les shadows écartent immédiatement un perdant, alors que le GSS compare aux shifts indépendamment des éliminations précédentes. Conserver ces politiques ou en démontrer l’équivalence avant de les fusionner. Les runtimes LL/LR restent séparés ; un partage justifié doit passer par `shared`, conformément aux instructions des moteurs.

**Scheduler : arrêt et attente.** [M-SCH-CANCEL-DRAIN](#finding-m-sch-cancel-drain) isole le travail résiduel après annulation : le corps utile est neutralisé mais des indices continuent d’être drainés. Une sortie plus rapide doit encore attendre les parts publiées et protéger les références de pile. [M-SCH-SELF-INDEX](#finding-m-sch-self-index) vise la recherche d’appartenance au pool, [M-SCH-CORUN-SWEEPS](#finding-m-sch-corun-sweeps) les scans sans progrès pendant la coopération, et [M-SCH-QUEUE-BASELINE](#finding-m-sch-queue-baseline) les capacités initiales par queue. Ce sont quatre causes différentes : un cache TLS ne résout pas les scans d’attente et une attente passive mal placée peut bloquer un travail nécessaire à la requête courante.

**Caches : distinguer contention et rétention.** [M-CAC-PUBLISHED-RECLAIM-LOCK](#finding-m-cac-published-reclaim-lock) décrit des destructions potentiellement volumineuses sous verrou writer, parfois sous le mutex d’index englobant. Le premier changement à étudier est la sortie des propriétaires devenus libérables, suivie de leur destruction hors verrou ; il faut conserver l’owner du raw publié et les masques de lecteurs. [M-CAC-COLLECT-LIFECYCLE](#finding-m-cac-collect-lifecycle) concerne le moment où la collecte finit par arriver. [M-CAC-CLEAR-ALLOC](#finding-m-cac-clear-alloc) concerne réallocation et capacité de table lors de clear. [M-CAC-DOCUMENT-PARTITIONS](#finding-m-cac-document-partitions) concerne des partitions créées même pour un lookup négatif. Ces constats ne justifient ni un `clear` sans changement de génération, ni un shrink concurrent, ni la fusion des epochs de table avec les stripes de `Published`. [M-CAC-MEMO-FACTOR](#finding-m-cac-memo-factor) a une frontière plus étroite : partager le protocole lookup/génération/provider/insertion en conservant first-wins et bypass documentaire.

**Workspace et protocoles.** Des scans d’identités historiques, des sets reconstruits et des index inverses construits sous mutex ajoutent du travail indépendamment des moteurs : [M-WSP-ACTIVE-DOCUMENTS](#finding-m-wsp-active-documents), [M-WSP-PENDING-REBUILD](#finding-m-wsp-pending-rebuild) et [M-WSP-INCOMING-BUILD-LOCK](#finding-m-wsp-incoming-build-lock). Les notifications des lecteurs ont aussi des conditions de progrès à préserver. Un cas particulièrement concret est [M-MCP-VALIDATE-DOUBLE-PARSE](#finding-m-mcp-validate-double-parse) : création textuelle déjà parsée, admission en document fermé puis update réinitialisant l’état conduisent à un second parse. `createDocument` étant protégé, le correctif doit prévoir une vraie entrée d’admission non parsée ; un appel direct depuis MCP ne compile pas. Ne pas supprimer le parse initial de toutes les factories pour résoudre ce seul chemin.

**Texte, AST/CST et sortie.** [M-TXT-UPDATE-COPIES](#finding-m-txt-update-copies) et [M-TXT-EDITS-SHIFTS](#finding-m-txt-edits-shifts) concernent deux contrats d’édition différents, coordonnées successives ou coordonnées originales. [M-AST-LIST-COUNT](#finding-m-ast-list-count), [M-AST-SORT-SCRATCH](#finding-m-ast-sort-scratch), [M-CST-NTH-COLLECT](#finding-m-cst-nth-collect) et [M-PUB-TRANSLATED-CST-COPY](#finding-m-pub-translated-cst-copy) ciblent des parcours ou buffers temporaires précis. Remplacer une copie par une vue impose de démontrer qui garde les octets vivants ; les AST et les textes traduits ne partagent pas nécessairement la même propriété. La projection d’entrée `SourceTranslation` et la construction de sortie `TextBuilder` ne doivent pas être fusionnées pour simuler une fonctionnalité de trace absente.

**Hôtes, exemples et livraison.** [M-HOST-EXTENSION-COPIES](#finding-m-host-extension-copies), [M-HOST-MCP-COPIES](#finding-m-host-mcp-copies) et [M-BLD-ESBUILD-COPIES](#finding-m-bld-esbuild-copies) permettent de centraliser des sources, tout en continuant à livrer des packages autonomes par copie/génération. [M-HOST-MEMORY-READER](#finding-m-host-memory-reader) requiert aussi d’adapter les copies du runner : une simple extraction avec nouvel import relatif casserait des VSIX ou scaffolds incomplets. Les budgets et cycles de vie des threads Node et navigateur ne sont pas interchangeables. Les buffers de protocole et l’anneau stdin sont des optimisations distinctes ([M-HOST-LSP-FRAME-COPIES](#finding-m-host-lsp-frame-copies), [M-HOST-STDIN-PUMP](#finding-m-host-stdin-pump)). Les oracles de test et de coût volontairement indépendants des moteurs doivent conserver cette indépendance.

## Consignes de reprise pour l’agent correcteur

1. Ouvrir le dépôt au commit `66c465bba3d2abd655e0e8fad3063f1fcfa9d599` ou comparer explicitement la branche cible à cette base. Relire `AGENTS.md` et `src/engines/AGENTS.md` pour toute modification des moteurs. Ne pas appliquer les numéros de ligne aveuglément à une révision plus récente.
2. Choisir un constat par changement cohérent. Rechercher le symbole et ses consommateurs, y compris headers publics, code généré en chaînes, templates, outils de test et branches de plateforme. Les liens ci-dessous sont des points d’entrée précis ; un décompte de recherches n’est pas une preuve suffisante de suppression.
3. Pour les trois suppressions locales, limiter le diff à la variable/branche démontrée. Pour les commentaires et chemins legacy, corriger la source canonique et régénérer ses artefacts seulement si nécessaire. Ne jamais éditer à la main `docs/cursiv-new.cmake` pour résoudre une duplication de ses templates.
4. Pour une optimisation, exposer avant modification le mécanisme de coût et le contrat à maintenir. Enlever une passe, une copie ou une réallocation évitable avant d’ajouter un cache global. Préserver ordre, égalité exacte, annulation, invalidation, propriété et comportement aux frontières lexicales.
5. Le présent audit n’autorise aucune affirmation de tests ou de gains déjà obtenus. Lors des correctifs, suivre les vérifications futures propres à la fiche et les instructions applicables du dépôt. Utiliser les scénarios existants pertinents ; les nombres de lectures, copies, allocations, octets retenus ou acquisitions de verrou sont plus informatifs qu’une promesse générique de rapidité. Aucun reproducer n’est livré ni demandé par cette campagne.
6. Pour chaque décision, noter le constat traité, le diff minimal, les invariants vérifiés et le résultat effectif. Si la piste ne convient pas, documenter la raison et conserver le constat comme écarté ou conditionnel ; ne pas forcer une factorisation pour solder un identifiant.

## Relecture contradictoire

Deux notes de relecture détaillent les vérifications indépendantes : [suppression et factorisation des moteurs](REVUE-SUPPRESSIONS-MOTEURS.md), [API et frontières de packaging](REVUE-SUPPRESSIONS-API.md). Leurs réserves importantes sont déjà incluses dans les fiches de ce document : elles ne sont pas une annexe indispensable à la reprise. Une dernière consolidation a vérifié les chevauchements entre les 158 constats sans identifier de doublon de cause supplémentaire. Ces relectures restent statiques.

## Index des constats

| Identifiant | Périmètre principal | Catégorie | Priorité | Preuve | Titre |
| --- | --- | --- | --- | --- | --- |
| [M-ASS-MARKER-REWRITES](#finding-m-ass-marker-rewrites) | ASS-001 | optimisation | P3 | candidat | Réécriture complète répétée du texte annoté |
| [M-ASS-VFS-CONTENT-COPIES](#finding-m-ass-vfs-content-copies) | ASS-002 | optimisation | P2 | établi | Les requêtes de dossiers du VFS copient le contenu des fichiers |
| [M-ASS-EXPECTED-COMMON](#finding-m-ass-expected-common) | ASS-003 | factorisation | P3 | établi | Duplication des helpers joined et baseOf dans les attentes |
| [M-ASS-DIAGNOSTIC-JSON-REENCODE](#finding-m-ass-diagnostic-json-reencode) | ASS-004 | optimisation | P3 | établi | Sérialisation répétée des objets durant le matching de payloads |
| [M-ASS-CST-HIDDEN-PAIRS](#finding-m-ass-cst-hidden-pairs) | ASS-006 | optimisation | P2 | établi | Recherche quadratique de doublons hidden dans l’oracle CST |
| [M-ASS-AST-EXCUSED-INDEX](#finding-m-ass-ast-excused-index) | ASS-007 | optimisation | P3 | établi | Chaque membre comparé rescane toutes les exclusions AST |
| [M-ASS-FIXEDSOURCE-STALE-COMMENT](#finding-m-ass-fixedsource-stale-comment) | ASS-009 | legacy | P3 | établi | Commentaire legacy sur un fixpoint de layout supprimé |
| [M-ASS-REPORT-QUANTILE-SORTS](#finding-m-ass-report-quantile-sorts) | ASS-014 | optimisation | P3 | établi | Le rapport retrie les mêmes coûts pour chaque quantile |
| [M-ASS-WARMUP-64-LIMIT](#finding-m-ass-warmup-64-limit) | ASS-015 | optimisation | P2 | établi | Le warmup ne peut réussir avant timeout pour 64 workers ou plus |
| [M-ASS-LSP-POSITIONS-ROOM](#finding-m-ass-lsp-positions-room) | ASS-018 | optimisation | P3 | candidat | Toutes les positions de mots stockées pour ne retenir que neuf sondes |
| [M-ASS-SESSION-TRANSPORT](#finding-m-ass-session-transport) | ASS-020 | factorisation | P3 | établi | Trois décodeurs LSP et quatre attentes de réponse maintenus séparément |
| [M-API-ENTRY-DOC](#finding-m-api-entry-doc) | API-006 | legacy | P3 | établi | Le commentaire public renvoie à getEntryRule absent de Grammar |
| [M-HOST-MEMORY-READER](#finding-m-host-memory-reader) | HOST-002 | factorisation | P2 | établi | Deux lecteurs de la section d’imports Wasm ont divergé |
| [M-HOST-DIRENT-BUFFERS](#finding-m-host-dirent-buffers) | HOST-004 | optimisation | P3 | établi | Les adaptateurs readdir matérialisent plusieurs buffers par entrée |
| [M-HOST-VFS-DIRECTORIES](#finding-m-host-vfs-directories) | HOST-007 | optimisation | P2 | établi | Chaque lookup de dossier rescane tous les fichiers virtuels |
| [M-HOST-STDIN-PUMP](#finding-m-host-stdin-pump) | HOST-009 | optimisation | P2 | établi | Copie octet par octet et retries multiples dans l’anneau stdin |
| [M-HOST-LSP-FRAME-COPIES](#finding-m-host-lsp-frame-copies) | HOST-010 | optimisation | P2 | établi | Le cadrage LSP recopie le préfixe à chaque fragment |
| [M-HOST-CANCEL-DISPOSABLE](#finding-m-host-cancel-disposable) | HOST-012 | simplification | P3 | candidat | L’abonnement d’annulation survit potentiellement à la requête |
| [M-HOST-DOCUMENT-INDEX](#finding-m-host-document-index) | HOST-013 | simplification | P3 | établi | Le montage du filesystem retrouve chaque index par indexOf |
| [M-HOST-SYMBOL-RECURSION](#finding-m-host-symbol-recursion) | HOST-016 | optimisation | P2 | établi | Conversion récursive des symboles documentaires dans Monaco |
| [M-HOST-EXTENSION-COPIES](#finding-m-host-extension-copies) | HOST-019 | factorisation | P2 | établi | La logique d’extension est copiée dans quatre exemples |
| [M-HOST-MCP-COPIES](#finding-m-host-mcp-copies) | HOST-021 | duplication | P2 | établi | Le bridge MCP a cinq sources équivalentes à maintenir |
| [M-BLD-LSP-EAGER](#finding-m-bld-lsp-eager) | BLD-002 | optimisation | P2 | candidat | La configuration core seule acquiert aussi lsp-framework |
| [M-BLD-PARSER-UNIT-COUNT](#finding-m-bld-parser-unit-count) | BLD-005 | duplication | P3 | établi | Le nombre d’unités générées est défini en C++ et en CMake |
| [M-BLD-ESBUILD-COPIES](#finding-m-bld-esbuild-copies) | BLD-012 | duplication | P3 | établi | La configuration esbuild est recopiée dans cinq projets |
| [M-BLD-DOCS-SETUP](#finding-m-bld-docs-setup) | BLD-017 | factorisation | P3 | candidat | La préparation et construction MkDocs ont deux définitions |
| [M-EXA-ARITH-PARCOURS-PROFONDEUR](#finding-m-exa-arith-parcours-profondeur) | EXA-002 | simplification | P2 | établi | Les parcours d’expressions utilisent la pile native proportionnellement à l’AST |
| [M-EXA-ARITH-CONTEXT-COPIES](#finding-m-exa-arith-context-copies) | EXA-002 | optimisation | P2 | établi | Chaque appel copie l’historique actif et crée une table de paramètres |
| [M-EXA-ARITH-UNIQUE-BUCKETS](#finding-m-exa-arith-unique-buckets) | EXA-003 | optimisation | P3 | établi | Les checks d’unicité allouent un vector pour chaque nom unique |
| [M-EXA-DM-PACKAGE-WALKS](#finding-m-exa-dm-package-walks) | EXA-009 | optimisation | P2 | établi | Les parcours de packages reconstruisent récursivement leurs préfixes |
| [M-EXA-DM-RENAME-EDIT-INDEX](#finding-m-exa-dm-rename-edit-index) | EXA-011 | optimisation | P2 | établi | Le renommage recherche chaque édition dans un vector croissant |
| [M-EXA-GENERATOR-PATH-DUP](#finding-m-exa-generator-path-dup) | EXA-013 | duplication | P3 | établi | Trois générateurs maintiennent la même politique de nom de sortie |
| [M-EXA-REQ-ENV-UNION](#finding-m-exa-req-env-union) | EXA-018 | optimisation | P2 | établi | Le check d’environnements recherche plusieurs fois la même union |
| [M-EXA-REQ-HTML-INVERSE](#finding-m-exa-req-html-inverse) | EXA-019 | optimisation | P2 | établi | Le rapport rescane toutes les arêtes de tests pour chaque exigence |
| [M-EXA-SM-TRANSITION-INDEX](#finding-m-exa-sm-transition-index) | EXA-023 | optimisation | P2 | établi | Le dédoublonnage quadratique des transitions est refait à chaque passe |
| [M-GRA-DERIVE-KEY](#finding-m-gra-derive-key) | GRA-004 | optimisation | P2 | établi | Sonder les formes dérivées sans copier leurs enfants sur un hit |
| [M-GRA-PRINT-EDGES](#finding-m-gra-print-edges) | GRA-008 | optimisation | P3 | établi | Dédupliquer les dépendances imprimées sans rescanner leur préfixe |
| [M-GRA-SUBTYPE-ENUM](#finding-m-gra-subtype-enum) | GRA-010 | optimisation | P2 | établi | Parcourir les bits présents lors de l’émission de la metadata AST |
| [M-GRA-WRAPPED-TWICE](#finding-m-gra-wrapped-twice) | GRA-012 | duplication | P3 | établi | Calculer une fois le texte de grammaire replié |
| [M-AST-METADATA-RESIDU](#finding-m-ast-metadata-residu) | AST-005 | legacy | P3 | établi | Nettoyer un include répété et le commentaire périmé de tag_of |
| [M-AST-VALUE-AS](#finding-m-ast-value-as) | AST-007 | simplification | P3 | établi | Réutiliser le résultat du cast de value_as |
| [M-AST-LIST-COUNT](#finding-m-ast-list-count) | AST-009 | optimisation | P2 | établi | Éviter de recompter le même suffixe pour chaque liste AST |
| [M-AST-SORT-SCRATCH](#finding-m-ast-sort-scratch) | AST-012 | optimisation | P2 | établi | Dimensionner le temporaire du tri AST sur les éléments réellement déplacés |
| [M-CST-LEAF-RESERVE](#finding-m-cst-leaf-reserve) | CST-004 | optimisation | P2 | candidat | Réduire la sur-réservation permanente de l’index des feuilles |
| [M-CST-NTH-COLLECT](#finding-m-cst-nth-collect) | CST-005 | optimisation | P2 | établi | Sélectionner un résultat CST sans matérialiser la liste complète |
| [M-TXT-PROJECTION-OSCILLATION](#finding-m-txt-projection-oscillation) | CST-007 | optimisation | P2 | établi | Borner les sauts du curseur de projection pour les spans non monotones |
| [M-TXT-UNICODE-LEGACY-PATH](#finding-m-txt-unicode-legacy-path) | TXT-003 | legacy | P2 | établi | Retirer l’ancien segment core du chemin de génération Unicode |
| [M-TXT-UPDATE-COPIES](#finding-m-txt-update-copies) | TXT-006 | optimisation | P2 | établi | Éviter une copie complète du document par changement incrémental |
| [M-TXT-EDITS-SHIFTS](#finding-m-txt-edits-shifts) | TXT-006 | optimisation | P2 | établi | Assembler le résultat des TextEdit en un passage |
| [M-TXT-TRIE-LOOKUP](#finding-m-txt-trie-lookup) | TXT-010 | optimisation | P2 | établi | Parcourir les segments URI par vues lors des recherches |
| [M-TXT-MEMFS-COPIES](#finding-m-txt-memfs-copies) | TXT-012 | simplification | P3 | établi | Construire le résultat binaire directement depuis le fichier en mémoire |
| [M-PUB-TRANSLATED-CST-COPY](#finding-m-pub-translated-cst-copy) | PUB-001 | optimisation | P2 | établi | Éviter la reconstruction complète du CST à chaque complétion traduite |
| [M-PUB-EXPECTED-SATURATED](#finding-m-pub-expected-saturated) | PUB-002 | simplification | P3 | établi | Arrêter l’énumération des spellings lorsque la sortie est pleine |
| [M-PUB-DOC-TOKEN-COPIES](#finding-m-pub-doc-token-copies) | PUB-004 | optimisation | P2 | établi | Réduire les copies entre fragments, tokens et nœuds documentaires |
| [M-PUB-JSON-FINITE-GUARD](#finding-m-pub-json-finite-guard) | PUB-005 | mort | P3 | établi | Retirer une branche de non-finitude inaccessible dans le sérialiseur privé |
| [M-PUB-DOC-RENDER-SHELL](#finding-m-pub-doc-render-shell) | PUB-011 | factorisation | P3 | établi | Partager l’assemblage externe des rendus de DocComment |
| [M-LL-OPERATOR-ACTIVE](#finding-m-ll-operator-active) | LL-005 | optimisation | P2 | établi | Indexer l’appartenance des règles actives lors du déroulement d’opérateurs |
| [M-LL-SHAPE-GRAPH](#finding-m-ll-shape-graph) | LL-006 | optimisation | P2 | établi | Construire une seule fois le graphe des analyses de forme LL |
| [M-LL-RETURN-CLOSURE](#finding-m-ll-return-closure) | LL-010 | optimisation | P2 | candidat | Mutualiser les fermetures epsilon des classes de retour |
| [M-LL-DFA-SETTLES](#finding-m-ll-dfa-settles) | LL-011 | optimisation | P2 | établi | Remplacer le fixpoint à scans complets du DFA par une file inverse |
| [M-LL-PLAIN-ENTRY](#finding-m-ll-plain-entry) | LL-013 | simplification | P2 | établi | Éviter LlMachine lorsque toute l’entrée est déjà classée plain |
| [M-LL-ENTRY-PROVENANCE](#finding-m-ll-entry-provenance) | LL-017 | optimisation | P2 | candidat | Éviter la reconstruction répétée de toute la provenance des chaînes AST |
| [M-LL-TLS-ROOM](#finding-m-ll-tls-room) | LL-026 | optimisation | P2 | établi | Borner la capacité conservée par les buffers du moteur d’épisode TLS |
| [M-LL-CELL-MEMBERSHIP](#finding-m-ll-cell-membership) | LL-030 | optimisation | P2 | établi | Indexer les piles déjà admises dans une cellule d’appels partagés |
| [M-LL-FRONTIER-INSERT](#finding-m-ll-frontier-insert) | LL-032 | optimisation | P2 | établi | Réduire les déplacements du front de positions trié |
| [M-LL-MEMO-UNREACHABLE-TAIL](#finding-m-ll-memo-unreachable-tail) | LL-033 | optimisation | P2 | établi | Limiter le stockage des entrées de mémo devenues hors horizon de consultation |
| [M-LL-FUTURE-PROJECTION](#finding-m-ll-future-projection) | LL-036 | factorisation | P3 | établi | Centraliser les champs scalaires du futur pour hash et égalité |
| [M-LL-CARET-REPLAY-WINDOWS](#finding-m-ll-caret-replay-windows) | LL-045 | optimisation | P2 | candidat | Attribuer puis réduire les replays de préfixes recouvrants au caret |
| [M-LEX-ALIAS-ACTIVE](#finding-m-lex-alias-active) | LEX-002 | optimisation | P2 | établi | Recherche linéaire de chaque alias dans la pile active |
| [M-LEX-GUARD-INTERN](#finding-m-lex-guard-intern) | LEX-005 | optimisation | P2 | établi | Internage linéaire des guards NFA |
| [M-LEX-PARTITION-QUEUED](#finding-m-lex-partition-queued) | LEX-007 | mort | P3 | établi | Tableau local queued écrit mais jamais lu |
| [M-LEX-WORDS-MULTICURSOR](#finding-m-lex-words-multicursor) | LEX-017 | optimisation | P2 | établi | L’index du premier octet est abandonné avec plusieurs curseurs |
| [M-LEX-GAP-BOUNDS](#finding-m-lex-gap-bounds) | LEX-021 | optimisation | P3 | candidat | Recalcul des bornes d’une même expression de gap |
| [M-LEX-GAP-SPAN-WORDS](#finding-m-lex-gap-span-words) | LEX-022 | optimisation | P2 | établi | Union et différence de spans traitées position par position |
| [M-LR-LOWER-DEPTH](#finding-m-lr-lower-depth) | LR-003 | simplification | P2 | établi | Les parcours de formes LR conservent une dépendance à la pile native |
| [M-LR-LOWER-GAP-INDEX](#finding-m-lr-lower-gap-index) | LR-003 | optimisation | P3 | établi | Le classement des règles de condition recherche à répétition dans les listes inverses |
| [M-LR-AUX-FIXPOINT-KEYS](#finding-m-lr-aux-fixpoint-keys) | LR-006 | optimisation | P2 | candidat | La fusion des auxiliaires reconstruit toutes les clés à chaque tour |
| [M-LR-GRAPH-GROWTH](#finding-m-lr-graph-growth) | LR-011 | optimisation | P2 | candidat | Les pools du graphe réservent leur capacité par bonds de seize |
| [M-LR-GUARD-FIXPOINT](#finding-m-lr-guard-fixpoint) | LR-012 | optimisation | P2 | établi | Le point fixe des exigences rescane toute la fermeture d’un état |
| [M-LR-GROUP-FLAG-SETS](#finding-m-lr-group-flag-sets) | LR-014 | optimisation | P3 | établi | Les lignes lexicales distinctes d’un groupe sont reconnues par recherche linéaire |
| [M-LR-ORDER-SCORE-DENSE](#finding-m-lr-order-score-dense) | LR-015 | optimisation | P2 | candidat | Chaque couple production-token interrogé alloue une ligne sur tout le domaine d’items |
| [M-LR-UNIT-EARLY-SNAPSHOT](#finding-m-lr-unit-early-snapshot) | LR-021 | optimisation | P2 | établi | L’élimination des unités copie l’automate avant de savoir si une cellule est éligible |
| [M-LR-MULTI-UNUSED-UNION](#finding-m-lr-multi-unused-union) | LR-024 | mort | P3 | établi | modeAct calcule une union qui n’est jamais relue |
| [M-LR-MULTI-ACTION-SPANS](#finding-m-lr-multi-action-spans) | LR-024 | optimisation | P2 | établi | Le test d’indifférence balaie et copie les lignes au lieu de chercher leurs deux runs |
| [M-LR-SITES-PROJECTION-CACHE](#finding-m-lr-sites-projection-cache) | LR-027 | optimisation | P2 | établi | La projection d’un site-set est reconstruite pour chaque action qui le lit |
| [M-LR-ELECT-TWIN-POOL](#finding-m-lr-elect-twin-pool) | LR-028 | optimisation | P2 | établi | Une classe de productions jumelles est recopiée pour chacun de ses membres |
| [M-LR-PROGRAM-SPARSE-BUILD](#finding-m-lr-program-sparse-build) | LR-030 | optimisation | P2 | candidat | La compression des programmes parcourt d’abord toutes les cellules absentes |
| [M-LR-EMIT-PEEK-INDEX](#finding-m-lr-emit-peek-index) | LR-035 | optimisation | P3 | candidat | La génération recherche chaque lookahead dans la collection globale |
| [M-LR-EMIT-GROUP-FRONT](#finding-m-lr-emit-group-front) | LR-036 | optimisation | P2 | candidat | Le regroupement par affinité rescane son front à chaque sélection |
| [M-LR-SPEC-MARK-PAIRS](#finding-m-lr-spec-mark-pairs) | LR-043 | optimisation | P2 | établi | Les marques de spéculation filtrent l’égalité sans éviter la comparaison de toutes les paires |
| [M-LR-LEXICAL-SHIFT-PAIRS](#finding-m-lr-lexical-shift-pairs) | LR-043 | factorisation | P3 | candidat | Deux politiques d’élimination encadrent le même prédicat lexical |
| [M-LR-GSS-CHUNKS-REUSE](#finding-m-lr-gss-chunks-reuse) | LR-045 | optimisation | P2 | établi | Un nouvel épisode GSS libère tous les blocs de ses arènes |
| [M-LR-GSS-ACCEPT-PATHS](#finding-m-lr-gss-accept-paths) | LR-050 | optimisation | P2 | candidat | L’acceptation développe les chemins de pile au-dessus de la forêt empaquetée |
| [M-LR-GSS-READPAST-PROPAGATION](#finding-m-lr-gss-readpast-propagation) | LR-051 | optimisation | P3 | candidat | read_past refait un balayage global des arcs jusqu’au point fixe |
| [M-LR-CACTUS-SIBLINGS](#finding-m-lr-cactus-siblings) | LR-053 | optimisation | P2 | établi | L’internement cactus parcourt linéairement les enfants d’un même parent |
| [M-LR-BOUND-CANDIDATE-INDEX](#finding-m-lr-bound-candidate-index) | LR-057 | optimisation | P2 | établi | Les candidats de borne sont dédoublonnés par balayage du résultat croissant |
| [M-LR-CARET-SCAN-INDEX](#finding-m-lr-caret-scan-index) | LR-064 | optimisation | P2 | établi | Les mémos lexicaux du caret recherchent leurs clés dans des vectors |
| [M-LR-CARET-REPAIR-INDEX](#finding-m-lr-caret-repair-index) | LR-065 | optimisation | P2 | établi | Chaque complétion d’un CST réparé rescane tous les records pour indexer les réparations |
| [M-LR-CARET-SAMPLE-UNION](#finding-m-lr-caret-sample-union) | LR-066 | optimisation | P3 | établi | L’union des samples est recalculée pour chaque contexte de collage du même terminal |
| [M-LR-CARET-FEATURE-ADMISSION](#finding-m-lr-caret-feature-admission) | LR-066 | optimisation | P3 | établi | Les shifts et features déjà émis sont recherchés linéairement |
| [M-LR-SAMPLE-CACHE-DENSE](#finding-m-lr-sample-cache-dense) | LR-067 | optimisation | P3 | candidat | Le premier sample alloue la matrice complète modes×terminaux |
| [M-LR-BITSET-MEMBERS](#finding-m-lr-bitset-members) | LR-069 | optimisation | P3 | établi | L’énumération par take rescane les mots vides depuis le début |
| [M-LSP-NOTIFICATION-BUILDS](#finding-m-lsp-notification-builds) | LSP-003 | optimisation | P2 | candidat | Les rafales déjà en file déclenchent un build par notification |
| [M-LSP-DEFER-CALLBACK](#finding-m-lsp-defer-callback) | LSP-009 | factorisation | P3 | établi | Mutualiser la remise des callbacks sortants |
| [M-LSP-CONFIG-UNCHANGED](#finding-m-lsp-config-unchanged) | LSP-016 | optimisation | P2 | candidat | Les configurations poussées identiques relancent la validation |
| [M-LSP-CONFIG-PER-DOCUMENT](#finding-m-lsp-config-per-document) | LSP-017 | optimisation | P2 | candidat | Les mêmes options de langue sont reconstruites par document |
| [M-PROTOCOL-DOCUMENT-ROUTE](#finding-m-protocol-document-route) | LSP-019 | factorisation | P2 | candidat | Partage partiel possible de l’acquisition documentaire LSP/MCP |
| [M-LSP-COMPLETION-RENDER-DUP](#finding-m-lsp-completion-render-dup) | LSP-025 | optimisation | P2 | candidat | Des items équivalents sont rendus avant la dédup finale |
| [M-LSP-REFERENCE-URI-REPEAT](#finding-m-lsp-reference-uri-repeat) | LSP-028 | optimisation | P2 | établi | Les réponses de références reparsent l’URI à chaque occurrence |
| [M-LSP-NAMED-RECOMPUTE](#finding-m-lsp-named-recompute) | LSP-030 | optimisation | P2 | candidat | Outline et coloration reconstruisent les mêmes tables de noms |
| [M-LSP-FORMAT-KEYWORD-FIRST](#finding-m-lsp-format-keyword-first) | LSP-035 | optimisation | P3 | établi | Le premier keyword est sélectionné après collecte de tous ses matches |
| [M-LSP-FORMAT-EDIT-COPIES](#finding-m-lsp-format-edit-copies) | LSP-036 | optimisation | P2 | établi | La finalisation du formatage recopie les textes d’édition deux fois |
| [M-LSP-ACTION-RECONVERT](#finding-m-lsp-action-reconvert) | LSP-041 | optimisation | P2 | établi | Le matching du même diagnostic est refait pour chaque quickfix |
| [M-MCP-ARGUMENT-COPY](#finding-m-mcp-argument-copy) | MCP-001 | optimisation | P2 | établi | tools/call duplique le JSON des arguments avant un appel en lecture |
| [M-MCP-SCRATCH-URI-CARDINALITY](#finding-m-mcp-scratch-uri-cardinality) | MCP-005 | optimisation | P2 | candidat | La réutilisation de slots ne borne pas les identités de scratch |
| [M-MCP-VALIDATE-DOUBLE-PARSE](#finding-m-mcp-validate-double-parse) | MCP-006 | optimisation | P2 | établi | validate parse deux fois le même scratch dans le chemin par défaut |
| [M-MCP-RESULT-COPY](#finding-m-mcp-result-copy) | MCP-007 | optimisation | P2 | établi | Plusieurs outils recopient le texte final dans leur résultat |
| [M-PROTOCOL-REFERENCE-COLLECT](#finding-m-protocol-reference-collect) | MCP-011 | factorisation | P2 | établi | Deux collectes multi-cibles maintiennent leurs propres règles de dédup |
| [M-MCP-RENAME-SEEN](#finding-m-mcp-rename-seen) | MCP-012 | optimisation | P2 | établi | La dédup des occurrences de renommage MCP est quadratique |
| [M-PROTOCOL-COMPLETION-SITE](#finding-m-protocol-completion-site) | MCP-015 | factorisation | P2 | établi | La construction du site de référence est recopiée dans MCP |
| [M-CGR-WRITTEN-PENDING](#finding-m-cgr-written-pending) | CGR-002 | optimisation | P2 | établi | Dédupliquer les réveils et les dépendances de WrittenReads |
| [M-CGR-BRANCH-KEYS](#finding-m-cgr-branch-keys) | CGR-005 | optimisation | P2 | établi | Dédupliquer les clés des branches sans recherche linéaire répétée |
| [M-CGR-OPTION-PAIRS](#finding-m-cgr-option-pairs) | CGR-007 | optimisation | P2 | établi | Éviter les comparaisons répétées des mêmes opérandes optionnels |
| [M-CGR-SPELLING-PREFIX](#finding-m-cgr-spelling-prefix) | CGR-008 | optimisation | P2 | établi | Limiter les copies des préfixes lors de l’énumération des opérateurs |
| [M-CGR-SELF-BOUND-READERS](#finding-m-cgr-self-bound-readers) | CGR-010 | simplification | P3 | établi | Remplacer la liste des lecteurs d’une séquence par un résumé de ses extrémités |
| [M-CGR-UNBOUNDED-SPELL](#finding-m-cgr-unbounded-spell) | CGR-014 | optimisation | P2 | établi | Calculer une fois les spellings utilisées pour dédupliquer les runs non bornés |
| [M-COM-ELECTION-HASH](#finding-m-com-election-hash) | COM-006 | optimisation | P3 | établi | L’élection recalcule les empreintes des mêmes histoires pendant le regroupement |
| [M-COM-TIED-PREFIX](#finding-m-com-tied-prefix) | COM-008 | optimisation | P2 | candidat | Chaque fusion d’insertions réinspecte le préfixe commun à la recherche d’un geste |
| [M-COM-TYPO-RANKED-TEMP](#finding-m-com-typo-ranked-temp) | COM-014 | optimisation | P3 | établi | Le classement des typos conserve trois tableaux de candidats et d’indices |
| [M-COM-FLATMAP-RESET](#finding-m-com-flatmap-reset) | COM-015 | optimisation | P2 | établi | Le reset des FlatMap réutilisées impose une nouvelle série de croissances |
| [M-COM-FLATMAP-DUP-GROW](#finding-m-com-flatmap-dup-grow) | COM-015 | optimisation | P3 | établi | L’internement peut croître avant de reconnaître une clé déjà présente |
| [M-COM-CHAIN-TEMP](#finding-m-com-chain-temp) | COM-022 | optimisation | P2 | établi | Les chaînes concaténées sont matérialisées avant de savoir si elles existent |
| [M-SCH-CANCEL-DRAIN](#finding-m-sch-cancel-drain) | SCH-003 | optimisation | P2 | établi | Le drainage continue index par index après annulation |
| [M-SCH-SELF-INDEX](#finding-m-sch-self-index) | SCH-006 | optimisation | P3 | candidat | Identifier le worker sans rescanner tout le pool |
| [M-SCH-CORUN-SWEEPS](#finding-m-sch-corun-sweeps) | SCH-009 | optimisation | P2 | candidat | Réexaminer les scans complets dans le tail de corun |
| [M-SCH-QUEUE-BASELINE](#finding-m-sch-queue-baseline) | SCH-011 | optimisation | P3 | candidat | Dimensionner la capacité initiale des queues du pool |
| [M-SCH-SPINLOCK-SURFACE](#finding-m-sch-spinlock-surface) | SCH-013 | simplification | P3 | candidat | Séparer le primitive pause de la classe SpinLock sans consommateur local |
| [M-CAC-CLEAR-ALLOC](#finding-m-cac-clear-alloc) | CAC-005 | optimisation | P2 | candidat | L’invalidation conservant capacité réalloue toute la table |
| [M-CAC-COLLECT-LIFECYCLE](#finding-m-cac-collect-lifecycle) | CAC-008 | optimisation | P2 | établi | La mémoire retirée attend une future activité de collecte |
| [M-CAC-PUBLISHED-RECLAIM-LOCK](#finding-m-cac-published-reclaim-lock) | CAC-010 | optimisation | P2 | établi | Détruire les snapshots libérés hors des verrous de publication |
| [M-CAC-MEMO-FACTOR](#finding-m-cac-memo-factor) | CAC-011 | factorisation | P3 | établi | Mutualiser le petit protocole get-or-compute des caches |
| [M-CAC-DOCUMENT-PARTITIONS](#finding-m-cac-document-partitions) | CAC-012 | optimisation | P2 | établi | Les lectures sans valeur créent des partitions conservées |
| [M-WSP-FACTORY-CANONICAL](#finding-m-wsp-factory-canonical) | WSP-001 | factorisation | P3 | candidat | La chaîne de factory répète normalisation et routage |
| [M-WSP-ACTIVE-DOCUMENTS](#finding-m-wsp-active-documents) | WSP-005 | optimisation | P2 | établi | all parcourt et réserve les identités historiques |
| [M-WSP-BUILD-OPTIONS-COPY](#finding-m-wsp-build-options-copy) | WSP-007 | optimisation | P3 | établi | Éviter la copie des catégories pour un test de validation |
| [M-WSP-DOCUMENT-WAKE-FANOUT](#finding-m-wsp-document-wake-fanout) | WSP-011 | optimisation | P2 | candidat | Les publications documentaires réveillent tous les documents attendus |
| [M-WSP-READ-EXIT-NOTIFY](#finding-m-wsp-read-exit-notify) | WSP-014 | optimisation | P2 | établi | Le départ de chaque lecteur diffuse un réveil sans changement d’admission |
| [M-WSP-IMPORTERS-HOMONYMS](#finding-m-wsp-importers-homonyms) | WSP-017 | optimisation | P2 | établi | Revisiter les mêmes importers pour chaque homonyme |
| [M-WSP-INCOMING-DOUBLE-RETIRE](#finding-m-wsp-incoming-double-retire) | WSP-018 | simplification | P3 | établi | Deux invalidations Incoming dans un même remplacement |
| [M-WSP-EXPORT-SUBSET](#finding-m-wsp-export-subset) | WSP-020 | optimisation | P3 | candidat | La visite d’un sous-ensemble parcourt tous les documents exportés |
| [M-WSP-INCOMING-BUILD-LOCK](#finding-m-wsp-incoming-build-lock) | WSP-021 | optimisation | P2 | candidat | La reconstruction complète des références entrantes tient le mutex global |
| [M-WSP-INITIAL-PARSE-SERIAL](#finding-m-wsp-initial-parse-serial) | WSP-025 | optimisation | P2 | candidat | Le parsing initial précède le pool de build et reste séquentiel |
| [M-REF-WAIT-LIST](#finding-m-ref-wait-list) | REF-010 | optimisation | P2 | candidat | Les publications observées et cycles parcourent une liste globale |
| [M-REF-POSITION-SCANS](#finding-m-ref-position-scans) | REF-013 | optimisation | P2 | établi | La navigation par position rescane toutes les références |
| [M-VAL-SPELLING-TOPK](#finding-m-val-spelling-topk) | VAL-005 | optimisation | P2 | établi | Les trois suggestions conservent et dédupliquent tous les noms proches |
| [M-SVC-SCHEDULER-COMMENT](#finding-m-svc-scheduler-comment) | SVC-001 | legacy | P3 | établi | Actualiser le contrat commenté des consommateurs du scheduler |
| [M-WSP-SHARD-DOCUMENT-SCANS](#finding-m-wsp-shard-document-scans) | WSP-027 | optimisation | P3 | candidat | Chaque shard reconstruit rescane toutes les rows documentaires |
| [M-WSP-PENDING-REBUILD](#finding-m-wsp-pending-rebuild) | WSP-028 | optimisation | P2 | établi | Déduplication et ordonnancement des documents reconstruits à chaque phase |


## Dossiers de correction autonomes

<a id="finding-m-ass-marker-rewrites"></a>
### M-ASS-MARKER-REWRITES — Réécriture complète répétée du texte annoté

**optimisation · P3 · candidat · confiance moyenne.** Périmètre principal : **ASS-001 — Marqueurs de texte et normalisation des positions attendues**. Périmètres associés : ASS-001.

**Frontière :** Texte annoté → texte réellement parsé, indices et plages attendues ; conversion vers les positions des requêtes.

**Observation :** Chaque marqueur déclenche trois find repartant du début et un erase qui déplace le suffixe. Des textes de N octets comportant M marqueurs peuvent ainsi refaire O(MN) travail, jusqu’au quadratique quand M suit N.

**Condition :** Grandes fixtures annotées à beaucoup de positions/ranges.

**Coût ou dette :** Recherches et déplacements de suffixes répétés ; faible importance probable pour les petites fixtures, sans mesure ici.

**Usages et visibilité :** Recherche textuelle dans src, tests, bench, examples, templates, tools et cmake. replaceIndices: 11 fichiers, dont src/test/cursiv/ExpectedFindReferences.cpp, src/test/cursiv/ExpectedCompletion.cpp, src/test/cursiv/ReplacedIndices.cpp, src/test/cursiv/ExpectedHover.cpp, src/test/cursiv/ExpectedGoToDefinition.cpp ; markedRange: 6 fichiers, dont src/test/cursiv/ExpectedFindReferences.cpp, src/test/cursiv/RangeUtils.hpp, src/test/cursiv/RangeUtils.cpp, src/test/cursiv/ExpectedGoToDefinition.cpp, src/test/cursiv/ExpectedRename.cpp ; textDocumentPositionParams: 3 fichiers, dont src/test/cursiv/TextDocumentParams.hpp, src/test/cursiv/TextDocumentParams.cpp, tests/support/ValidationHelper_test.cpp. Les instanciations et appels émis comme chaînes restent inclus dans cette lecture ; ce décompte ne prouve pas une utilisation externe.

**Changement proposé :** Étudier un scanner conservant un curseur d’entrée et construisant output une fois. Avant changement, préciser si les marqueurs apparus par concaténation des côtés d’un erase doivent encore être interprétés ; si oui, prévoir une petite reprise locale aux frontières.

**Invariants à préserver :** Priorité des marqueurs au même offset, coordonnées en octets du texte nettoyé, imbrication et tri stable des ranges. Marqueurs personnalisés, préfixes communs, marqueur vide et ranges déséquilibrées demandent une décision compatible.

**Validation future — non exécutée :** Après correction : égalité exacte output/indices/ranges sur les fixtures existantes et les interactions de marqueurs personnalisés ; ne pas transformer la tâche en création de reproducer ici.

**Preuves dans la révision auditée :**

- [src/test/cursiv/ReplacedIndices.cpp:27](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/test/cursiv/ReplacedIndices.cpp#L27) — find sur tout input à chaque tour
- [src/test/cursiv/ReplacedIndices.cpp:48](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/test/cursiv/ReplacedIndices.cpp#L48) — erase décale le suffixe
- [src/test/cursiv/ReplacedIndices.cpp:51](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/test/cursiv/ReplacedIndices.cpp#L51) — Tri stable de la sortie

<a id="finding-m-ass-vfs-content-copies"></a>
### M-ASS-VFS-CONTENT-COPIES — Les requêtes de dossiers du VFS copient le contenu des fichiers

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **ASS-002 — État initial, nettoyage et réalisme des workspaces de test**. Périmètres associés : ASS-002.

**Frontière :** Services et texte de fixture → document construit puis retrait des documents et ressources de test.

**Observation :** isDirectory appelle findAll(uri).empty ; readDirectory récupère un vector<Entry> de tous les descendants alors qu’il ne consulte ensuite que Entry.uri. UriTrie::collectValues copie T, et Entry contient une string content. Une vérification booléenne ou un listing copie donc potentiellement l’intégralité des contenus du sous-arbre.

**Condition :** Fixtures de workspace larges contenant de gros documents, ou multiples stat/exists/readDirectory.

**Coût ou dette :** O(S) octets de contenu copiés par requête de sous-arbre, S somme des contenus descendants, allocations incluses ; readDirectory déduplique en plus les noms par recherche linéaire.

**Usages et visibilité :** Recherche textuelle dans src, tests, bench, examples, templates, tools et cmake. parseHelper: 6 fichiers, dont src/test/cursiv/ParseHelper.cpp, src/test/cursiv/ParseHelper.hpp, src/test/cursiv/DecodedSemanticTokens.cpp, src/test/cursiv/ValidationHelper.cpp, tests/support/ValidationHelper_test.cpp ; clearDocuments: 4 fichiers, dont src/test/cursiv/DocumentDisposable.cpp, src/test/cursiv/ParseHelper.cpp, src/test/cursiv/ParseHelper.hpp, tests/support/ValidationHelper_test.cpp ; documentDisposable: 12 fichiers, dont src/test/cursiv/ExpectedFindReferences.cpp, src/test/cursiv/DocumentDisposable.cpp, src/test/cursiv/ExpectedSymbols.cpp, src/test/cursiv/ExpectFormatting.cpp, src/test/cursiv/ExpectedCompletion.cpp ; VirtualFileSystemProvider: 3 fichiers, dont src/test/cursiv/VirtualFileSystemProvider.hpp, src/test/cursiv/VirtualFileSystemProvider.cpp, tests/support/ValidationHelper_test.cpp. Les instanciations et appels émis comme chaînes restent inclus dans cette lecture ; ce décompte ne prouve pas une utilisation externe.

**Changement proposé :** Ajouter au besoin une visite non possédante / un test d’existence de descendants au trie, ou garder un index de métadonnées dans le VFS ; utiliser uniquement URI/références pour le listing. Conserver findAll comme API de copie si des consommateurs la demandent.

**Invariants à préserver :** Ne pas modifier le ownership des contenus de readFile ; durée des références bornée à la requête sans mutation du trie. Préserver normalize_uri, équivalence slash final, distinction fichier/dossier et tri des noms.

**Validation future — non exécutée :** Après correction : mêmes résultats stat/exists/readDirectory pour sous-arbres, dossier vide et URI normalisées ; contrôler ultérieurement les copies de contenu. Vérification statique seulement pendant cette campagne.

**Preuves dans la révision auditée :**

- [src/test/cursiv/VirtualFileSystemProvider.cpp:37](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/test/cursiv/VirtualFileSystemProvider.cpp#L37) — findAll pour un booléen
- [src/test/cursiv/VirtualFileSystemProvider.cpp:66](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/test/cursiv/VirtualFileSystemProvider.cpp#L66) — Copies Entry pour listing
- [src/test/cursiv/VirtualFileSystemProvider.hpp:33](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/test/cursiv/VirtualFileSystemProvider.hpp#L33) — Entry contient URI et contenu
- [src/core/cursiv/utils/UriTrie.hpp:163](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/utils/UriTrie.hpp#L163) — Copie de chaque valeur du trie

<a id="finding-m-ass-expected-common"></a>
### M-ASS-EXPECTED-COMMON — Duplication des helpers joined et baseOf dans les attentes

**factorisation · P3 · établi · confiance forte.** Périmètre principal : **ASS-003 — Projection et comparaison des résultats de complétion et de symboles**. Périmètres associés : ASS-003.

**Frontière :** Résultat de complétion ou de symboles → champs et ordre retenus → valeur soumise au comparateur attendu.

**Observation :** ExpectedCompletion.cpp et ExpectedSymbols.cpp définissent chacun un joined(span<string>) identique et un baseOf visitant les mêmes six champs des variantes. Une évolution d’ExpectedBase ou du rendu des listes doit être reportée dans deux implémentations.

**Condition :** Maintenance des champs de fixtures ou des messages d’attente.

**Coût ou dette :** Deux copies exactes de helpers privés ; pas de promesse de gain runtime.

**Usages et visibilité :** Recherche textuelle dans src, tests, bench, examples, templates, tools et cmake. expectCompletion: 6 fichiers, dont src/test/cursiv/ExpectedCompletion.hpp, src/test/cursiv/ExpectedCompletion.cpp, tests/support/Editor.hpp, tests/support/ValidationHelper_test.cpp, tests/lsp/completion/completion_test.cpp ; expectSymbols: 4 fichiers, dont src/test/cursiv/ExpectedSymbols.cpp, src/test/cursiv/ExpectedSymbols.hpp, tests/support/ValidationHelper_test.cpp, tests/lsp/navigation_test.cpp ; ExpectedBase: 13 fichiers, dont src/test/cursiv/ExpectedFindReferences.cpp, src/test/cursiv/ExpectedSymbols.cpp, src/test/cursiv/ExpectedCompletion.cpp, src/test/cursiv/ExpectedBase.hpp, src/test/cursiv/ReplacedIndices.cpp ; rankOf: 1 fichiers, dont src/test/cursiv/ExpectedCompletion.cpp. Les instanciations et appels émis comme chaînes restent inclus dans cette lecture ; ce décompte ne prouve pas une utilisation externe.

**Changement proposé :** Extraire deux helpers internes à la bibliothèque de test : projection des champs communs par visite de variante, et rendu d’une liste de chaînes. Réutiliser seulement ces helpers, sans créer une abstraction générale des providers.

**Invariants à préserver :** Conserver signatures publiques expectCompletion/expectSymbols, ordre de projection, ordre stable de completion, callback optionnel, disposeAfterCheck et messages de taille propres à chaque attente.

**Validation future — non exécutée :** Après correction : mêmes messages et champs de fixture pour les variantes liste/callback ; vérifier les consommateurs publics de ExpectedBase lors de tout changement de structure.

**Preuves dans la révision auditée :**

- [src/test/cursiv/ExpectedCompletion.cpp:30](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/test/cursiv/ExpectedCompletion.cpp#L30) — joined identique
- [src/test/cursiv/ExpectedCompletion.cpp:39](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/test/cursiv/ExpectedCompletion.cpp#L39) — baseOf identique
- [src/test/cursiv/ExpectedSymbols.cpp:26](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/test/cursiv/ExpectedSymbols.cpp#L26) — Deuxième joined
- [src/test/cursiv/ExpectedSymbols.cpp:35](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/test/cursiv/ExpectedSymbols.cpp#L35) — Deuxième baseOf

<a id="finding-m-ass-diagnostic-json-reencode"></a>
### M-ASS-DIAGNOSTIC-JSON-REENCODE — Sérialisation répétée des objets durant le matching de payloads

**optimisation · P3 · établi · confiance forte.** Périmètre principal : **ASS-004 — Filtrage des diagnostics et sens des assertions positives/négatives**. Périmètres associés : ASS-004.

**Frontière :** Résultat de validation → sélection par plage, sévérité, contenu et données → assertion de présence ou d’absence.

**Observation :** isDiagnosticDataEqual sérialise entièrement lhs et rhs avant de tester si ce sont des objets. Si les objets diffèrent mais rhs est un sous-ensemble, chaque appel récursif sérialise à nouveau son sous-arbre. Des chaînes d’objets imbriqués avec des propriétés supplémentaires peuvent cumuler un travail quadratique dans leur profondeur.

**Condition :** Attentes de payloads JSON imbriqués et partiels, comparées à plusieurs diagnostics.

**Coût ou dette :** Allocations des deux chaînes JSON et tris des clés de chaque sous-objet répétés à plusieurs profondeurs ; coût lié à la taille cumulée des sous-arbres.

**Usages et visibilité :** Recherche textuelle dans src, tests, bench, examples, templates, tools et cmake. isDiagnosticDataEqual: 3 fichiers, dont src/test/cursiv/ExpectDiagnosticOptions.hpp, src/test/cursiv/ExpectDiagnosticOptions.cpp, tests/support/ValidationHelper_test.cpp ; filterByOptions: 6 fichiers, dont src/test/cursiv/ExpectDiagnosticOptions.hpp, src/test/cursiv/ExpectDiagnosticOptions.cpp, examples/statemachine/tests/grammar_test.cpp, examples/arithmetics/tests/grammar_test.cpp, examples/requirements/tests/grammar_test.cpp ; expectNoIssues: 8 fichiers, dont src/test/cursiv/ExpectDiagnosticOptions.hpp, src/test/cursiv/ExpectDiagnosticOptions.cpp, src/test/cursiv/ValidationHelper.cpp, tests/support/ValidationHelper_test.cpp, examples/statemachine/tests/grammar_test.cpp ; validationHelper: 7 fichiers, dont src/test/cursiv/CodeActionResult.cpp, src/test/cursiv/ValidationHelper.hpp, src/test/cursiv/ValidationHelper.cpp, tests/support/ValidationHelper_test.cpp, examples/statemachine/tests/grammar_test.cpp. Les instanciations et appels émis comme chaînes restent inclus dans cette lecture ; ce décompte ne prouve pas une utilisation externe.

**Changement proposé :** Traiter en premier le cas deux objets par la comparaison récursive des seules clés attendues. Garder dans un premier correctif la comparaison sérialisée pour les valeurs non objets/arrays afin de préserver leurs règles numériques et de rendu.

**Invariants à préserver :** Ce helper est un matcher de sous-objet : rhs vide accepte un objet lhs non vide ; ne pas le remplacer par une égalité stricte. Conserver comparaison des arrays, nombres, null et distinction objet/non-objet ; ne pas changer JsonValue public pour ce seul helper.

**Validation future — non exécutée :** Après correction : vérifier les objets égaux/partiels, clés absentes, arrays d’objets et valeurs numériques ; comparer les verdicts à l’implémentation antérieure avant toute optimisation supplémentaire.

**Preuves dans la révision auditée :**

- [src/test/cursiv/ExpectDiagnosticOptions.cpp:56](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/test/cursiv/ExpectDiagnosticOptions.cpp#L56) — Sérialisation entière avant test du type
- [src/test/cursiv/ExpectDiagnosticOptions.cpp:58](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/test/cursiv/ExpectDiagnosticOptions.cpp#L58) — Récursion sur les seules clés attendues
- [src/core/cursiv/services/JsonValue.hpp:12](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/services/JsonValue.hpp#L12) — Sérialisation déterministe des clés/nombres

<a id="finding-m-ass-cst-hidden-pairs"></a>
### M-ASS-CST-HIDDEN-PAIRS — Recherche quadratique de doublons hidden dans l’oracle CST

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **ASS-006 — Oracle CST et rôle des empreintes de tape**. Périmètres associés : ASS-006.

**Frontière :** CST produit → verdict d’invariants structurels et éventuelle empreinte de comparaison.

**Observation :** audit_cst_structure parcourt chaque record hidden non vide puis tous les records suivants pour comparer les spans. Avec H records hidden et N records, le contrôle effectue O(HN) comparaisons même si tous les spans diffèrent.

**Condition :** Campagnes structurelles sur des CST riches en trivia/commentaires ou longues listes de records.

**Coût ou dette :** Jusqu’à O(N²) comparaisons pour démontrer l’absence de doublons ; le nombre P de paires réellement signalées est une borne de sortie distincte.

**Usages et visibilité :** Recherche textuelle dans src, tests, bench, examples, templates, tools et cmake. CstInvariants: 2 fichiers, dont src/fuzz/cursiv/CstInvariants.hpp, src/fuzz/cursiv/FuzzRun.hpp ; audit_cst_structure: 4 fichiers, dont tests/engines/shared/CstAudit.hpp, tests/engines/lr/LrAsi_test.cpp, tests/engines/lr/TapeAudit_test.cpp, tests/torture/main.cpp ; TapeSeal: 2 fichiers, dont src/fuzz/cursiv/TapeSeal.hpp, src/fuzz/cursiv/FuzzRun.hpp ; tape_seal: 1 fichiers, dont src/fuzz/cursiv/FuzzRun.hpp. Les instanciations et appels émis comme chaînes restent inclus dans cette lecture ; ce décompte ne prouve pas une utilisation externe.

**Changement proposé :** Construire un index span(begin,end) → liste ordonnée d’ids hidden non vides. Parcourir ensuite les ids initiaux et émettre seulement les partenaires suivants de leur groupe, pour conserver l’ordre des diagnostics.

**Invariants à préserver :** Le code actuel considère hidden même si subtreeSize>1 : ne pas ajouter implicitement un filtre leaf. Garder toutes les paires (et pas seulement la première) et les messages A10, sans supposer la monotonie d’un CST justement en cours de validation.

**Validation future — non exécutée :** Après correction : comparer la liste ordonnée complète des A10 pour spans distincts, plusieurs doublons et CST désordonnés ; vérifier une croissance hors sortie proche de N log N ou N selon l’index choisi.

**Preuves dans la révision auditée :**

- [tests/engines/shared/CstAudit.hpp:98](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/tests/engines/shared/CstAudit.hpp#L98) — Boucle outer hidden
- [tests/engines/shared/CstAudit.hpp:100](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/tests/engines/shared/CstAudit.hpp#L100) — Tous les records suivants
- [tests/engines/shared/CstAudit.hpp:103](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/tests/engines/shared/CstAudit.hpp#L103) — Une sortie par paire trouvée

<a id="finding-m-ass-ast-excused-index"></a>
### M-ASS-AST-EXCUSED-INDEX — Chaque membre comparé rescane toutes les exclusions AST

**optimisation · P3 · établi · confiance forte.** Périmètre principal : **ASS-007 — Comparateur AST, lecteurs de valeurs et exclusions**. Périmètres associés : ASS-007.

**Frontière :** AST attendu et obtenu → comparaison de types, valeurs et enfants ; signalement des membres aveugles ou excusés.

**Observation :** drop appelle skipped pour chaque valeur/enfant, et skipped parcourt tout excused jusqu’à trouver le couple node/member. enter applique drop aux deux côtés en utilisant volontairement le nœud attendu a. Le même ensemble d’exclusions est donc reconsulté linéairement durant tout le parcours.

**Condition :** Grand AST avec de nombreux membres que le générateur ne sait pas écrire.

**Coût ou dette :** O(ME) recherches, M membres filtrés et E exclusions ; aucun coût notable établi lorsque E est vide ou minuscule.

**Usages et visibilité :** Recherche textuelle dans src, tests, bench, examples, templates, tools et cmake. AstCompare: 4 fichiers, dont src/fuzz/cursiv/AstCompare.hpp, src/fuzz/cursiv/CstInvariants.hpp, src/fuzz/cursiv/FuzzRun.hpp, tests/fuzz/infix_entry.cpp ; AstExcused: 5 fichiers, dont src/fuzz/cursiv/AstCompare.hpp, src/fuzz/cursiv/GrammarGenerator.hpp, src/fuzz/cursiv/AstExcused.hpp, src/fuzz/cursiv/GrammarDerivation.hpp, src/fuzz/cursiv/FuzzRun.hpp ; Render: 2 fichiers, dont src/fuzz/cursiv/CursivActions.hpp, src/core/cursiv/documentation/DocComment.cpp ; ast_of: 20 fichiers, dont src/test/cursiv/AstText.cpp, src/test/cursiv/AstText.hpp, tests/oracle/RgramVerdict.hpp, tests/oracle/rgram_lane.cpp, tests/engines/shared/AstMaterialise_cases.cpp. Les instanciations et appels émis comme chaînes restent inclus dans cette lecture ; ce décompte ne prouve pas une utilisation externe.

**Changement proposé :** Créer au début de run un index local par couple (adresse du nœud attendu, nom du membre), ou grouper les exclusions par nœud. Conserver un chemin direct lorsque excused est vide.

**Invariants à préserver :** Filtrer les deux arbres selon le nœud attendu a, pas le nœud obtenu b ; préserver les noms, l’ordre des valeurs/enfants et le comptage blind. Les noms et adresses n’ont de durée de vie que celle de run.

**Validation future — non exécutée :** Après correction : mêmes divergences et mêmes listes blind avec exclusions absentes, répétées, noms identiques sur plusieurs nœuds, et exclusions de valeurs/enfants.

**Preuves dans la révision auditée :**

- [src/fuzz/cursiv/AstCompare.hpp:78](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/fuzz/cursiv/AstCompare.hpp#L78) — Recherche de couple dans toutes les exclusions
- [src/fuzz/cursiv/AstCompare.hpp:137](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/fuzz/cursiv/AstCompare.hpp#L137) — Appel pour chaque membre
- [src/fuzz/cursiv/AstCompare.hpp:167](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/fuzz/cursiv/AstCompare.hpp#L167) — Deux côtés filtrés selon a
- [src/fuzz/cursiv/AstCompare.hpp:192](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/fuzz/cursiv/AstCompare.hpp#L192) — Même principe pour enfants

<a id="finding-m-ass-fixedsource-stale-comment"></a>
### M-ASS-FIXEDSOURCE-STALE-COMMENT — Commentaire legacy sur un fixpoint de layout supprimé

**legacy · P3 · établi · confiance forte.** Périmètre principal : **ASS-009 — Choix pseudoaléatoires, rejouabilité et comptage des cas jugés**. Périmètres associés : ASS-009.

**Frontière :** Seed et nombre de documents → décisions, tentatives complètes, cas jugés et résumé de campagne.

**Observation :** FixedSource.hpp décrit encore un usage par le layout fixpoint pour ajouter des séparateurs sans consommer d’entropie. Le code actuel GrammarGenerator::lay_out assemble une fois et dit explicitement no fixpoint ; FixedSource reste réellement utilisé dans CursivLexicon pour choisir le séparateur.

**Condition :** Lecture/maintenance du contrat des sources de décisions.

**Coût ou dette :** Documentation contradictoire qui peut faire chercher un algorithme absent ; aucun coût runtime.

**Usages et visibilité :** Recherche textuelle dans src, tests, bench, examples, templates, tools et cmake. DecisionSource: 6 fichiers, dont src/fuzz/cursiv/SeededSource.hpp, src/fuzz/cursiv/FixedSource.hpp, src/fuzz/cursiv/CursivLexicon.hpp, src/fuzz/cursiv/GrammarGenerator.hpp, src/fuzz/cursiv/DecisionSource.hpp ; SeededSource: 4 fichiers, dont src/fuzz/cursiv/SeededSource.hpp, src/fuzz/cursiv/FuzzRun.hpp, tests/metric/RepairMetric_test.cpp, bench/recovery_metric/support/FaultCorpus.hpp ; FixedSource: 2 fichiers, dont src/fuzz/cursiv/FixedSource.hpp, src/fuzz/cursiv/CursivLexicon.hpp ; Lane: 7 fichiers, dont src/fuzz/cursiv/FuzzRun.hpp, src/test/cursiv/RecoveryLane.hpp, tests/engines/ll/LlReservedBound_test.cpp, examples/statemachine/tests/recovery_test.cpp, examples/arithmetics/tests/recovery_test.cpp. Les instanciations et appels émis comme chaînes restent inclus dans cette lecture ; ce décompte ne prouve pas une utilisation externe.

**Changement proposé :** Actualiser le commentaire de FixedSource pour décrire le rendu canonique du séparateur et les probes déterministes encore présents. Ne pas supprimer la classe.

**Invariants à préserver :** SeededSource et le nombre de décisions consommées restent inchangés ; aucune migration du format seedé n’est nécessaire.

**Validation future — non exécutée :** Rechercher les usages FixedSource après le diff et vérifier que chaque usage décrit existe encore ; validation documentaire suffisante.

**Preuves dans la révision auditée :**

- [src/fuzz/cursiv/FixedSource.hpp:1](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/fuzz/cursiv/FixedSource.hpp#L1) — Ancienne description layout fixpoint
- [src/fuzz/cursiv/GrammarGenerator.hpp:431](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/fuzz/cursiv/GrammarGenerator.hpp#L431) — Assemblage sans fixpoint explicite
- [src/fuzz/cursiv/CursivLexicon.hpp:496](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/fuzz/cursiv/CursivLexicon.hpp#L496) — Consommateur actif à conserver

<a id="finding-m-ass-report-quantile-sorts"></a>
### M-ASS-REPORT-QUANTILE-SORTS — Le rapport retrie les mêmes coûts pour chaque quantile

**optimisation · P3 · établi · confiance forte.** Périmètre principal : **ASS-014 — Agrégation, signatures et régénération des résultats attendus**. Périmètres associés : ASS-014.

**Frontière :** Verdicts individuels → totaux par classe/langage, signatures de référence et rapports texte/JSON.

**Observation :** RepairTally::cost copie costs et trie la copie à chaque quantile. RepairReport::text appelle cost pour 0.5,0.99,1.0 et json recommence les trois calculs. main imprime toujours le texte puis éventuellement le JSON sans ajout de mesures entre ces sorties.

**Condition :** Rapports de campagne avec beaucoup de coûts par classe, surtout avec export JSON.

**Coût ou dette :** Trois ou six copies et tris O(C log C) du même vecteur de C mesures par ligne, sans nécessité sémantique.

**Usages et visibilité :** Recherche textuelle dans src, tests, bench, examples, templates, tools et cmake. RepairTally: 3 fichiers, dont bench/recovery_metric/main.cpp, bench/recovery_metric/support/RepairTally.hpp, bench/recovery_metric/support/RepairReport.hpp ; RepairReport: 2 fichiers, dont bench/recovery_metric/main.cpp, bench/recovery_metric/support/RepairReport.hpp ; signature: 16 fichiers, dont src/test/cursiv/RecoveryLane.hpp, src/engines/cursiv/shared/gen/LexAlphabet.hpp, src/engines/cursiv/shared/gen/LexWalkEmitter.hpp, src/engines/cursiv/lr/gen/lr_emit_parser.cpp, src/core/cursiv/validation/ValidationRegistry.hpp ; reference_path: 1 fichiers, dont src/test/cursiv/RecoveryLane.hpp. Les instanciations et appels émis comme chaînes restent inclus dans cette lecture ; ce décompte ne prouve pas une utilisation externe.

**Changement proposé :** Produire un résumé p50/p99/max en une seule copie triée par tally au moment de finaliser le rapport, ou passer une vue de coûts triés aux deux sérialisations. Garder l’API cost si elle a d’autres consommateurs, en délégant à un helper de rang.

**Invariants à préserver :** Conserver nearest-rank, valeur −1 sans mesure, précision des doubles, ordre des champs et possibilité d’ajouter des mesures avant finalisation. Si cache mutable, invalider après add et éviter d’introduire un état partagé non protégé.

**Validation future — non exécutée :** Après correction : égalité des trois valeurs pour vide, singleton, nombres pairs/impairs et doublons, puis égalité des sorties texte/JSON ; aucun benchmark lancé dans cet audit.

**Preuves dans la révision auditée :**

- [bench/recovery_metric/support/RepairTally.hpp:81](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/bench/recovery_metric/support/RepairTally.hpp#L81) — Une copie/tri pour chaque demande
- [bench/recovery_metric/support/RepairReport.hpp:99](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/bench/recovery_metric/support/RepairReport.hpp#L99) — Trois demandes texte
- [bench/recovery_metric/support/RepairReport.hpp:166](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/bench/recovery_metric/support/RepairReport.hpp#L166) — Trois demandes JSON
- [bench/recovery_metric/main.cpp:345](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/bench/recovery_metric/main.cpp#L345) — Deux sorties consécutives sans nouvelle mesure

<a id="finding-m-ass-warmup-64-limit"></a>
### M-ASS-WARMUP-64-LIMIT — Le warmup ne peut réussir avant timeout pour 64 workers ou plus

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **ASS-015 — Fenêtres de temps, phases et échauffement**. Périmètres associés : ASS-015.

**Frontière :** Charge préparée → exécution mesurée par phase → durée publiée.

**Observation :** warm_pool crée range(64,0) et n’arrête un tour pour couverture que si seen.size()>workers. Chaque callback n’ajoute qu’un identifiant de thread ; un tour observe donc au plus64 identifiants. Pour workers>=64, cette condition est impossible indépendamment de la répartition réelle. Les tours occupent activement les CPU jusqu’au deadline de deux secondes ; multi_stages recommence pour chaque répétition.

**Condition :** Courbes multi-documents contenant un point configuré à 64 workers ou davantage. Pour moins de workers, la couverture réelle dépend encore du scheduler.

**Coût ou dette :** Warmup pouvant atteindre systématiquement le plafond d’environ2s, hors fenêtre chronométrée, à chaque itération et point concerné ; CPU occupé par des tâches de500µs. Aucun temps mesuré dans cet audit.

**Usages et visibilité :** Recherche textuelle dans src, tests, bench, examples, templates, tools et cmake. PhaseClock: 1 fichiers, dont src/bench/cursiv/LanguageBench.hpp ; unparsed_input: 1 fichiers, dont src/bench/cursiv/LanguageBench.hpp ; ws_stages: 2 fichiers, dont src/bench/cursiv/LanguageBench.hpp, tests/bench/bench_test.cpp ; warm_pool: 1 fichiers, dont src/bench/cursiv/LanguageBench.hpp ; multi_stages: 2 fichiers, dont src/bench/cursiv/LanguageBench.hpp, tests/bench/bench_test.cpp. Les instanciations et appels émis comme chaînes restent inclus dans cette lecture ; ce décompte ne prouve pas une utilisation externe.

**Changement proposé :** Définir d’abord la couverture recherchée (workers réels seuls ou workers plus caller), puis dimensionner le nombre de tâches et le seuil à partir de cette valeur. Employer une coordination de démarrage explicite ou un protocole borné qui ne dépend pas d’un nombre fixe64.

**Invariants à préserver :** Ne pas supprimer l’échauffement nécessaire aux courbes ni le confondre avec le temps de build. Vérifier le nombre effectif de threads exposé par TaskScheduler, le rôle du caller et les configurations où moins de threads participent. Garder une borne de sortie si la couverture n’est pas réalisable.

**Validation future — non exécutée :** Après correction : revue arithmétique de la condition pour0/1/63/64/>64 ; vérifier ultérieurement que l’échauffement termine quand la couverture cible est atteinte et que setup/warmup restent hors chrono. Aucun scheduler exécuté pendant la campagne.

**Preuves dans la révision auditée :**

- [src/bench/cursiv/LanguageBench.hpp:725](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/bench/cursiv/LanguageBench.hpp#L725) — Seulement64 tâches par tour
- [src/bench/cursiv/LanguageBench.hpp:732](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/bench/cursiv/LanguageBench.hpp#L732) — Boucle CPU de500µs dans chaque tâche
- [src/bench/cursiv/LanguageBench.hpp:740](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/bench/cursiv/LanguageBench.hpp#L740) — Seuil strictement supérieur à workers
- [src/bench/cursiv/LanguageBench.hpp:755](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/bench/cursiv/LanguageBench.hpp#L755) — Reprise du warmup pour chaque répétition

<a id="finding-m-ass-lsp-positions-room"></a>
### M-ASS-LSP-POSITIONS-ROOM — Toutes les positions de mots stockées pour ne retenir que neuf sondes

**optimisation · P3 · candidat · confiance moyenne.** Périmètre principal : **ASS-018 — Sondes de services et signification des distributions**. Périmètres associés : ASS-018.

**Frontière :** Document construit et service installé → positions sondées, appels répétés et distribution de durées/résultats.

**Observation :** positions remplit starts avec chaque début de mot du document puis sélectionne au maximum9 indices uniformément espacés. Le vecteur temporaire peut donc être proportionnel au nombre de mots alors que le résultat reste borné à10 positions avec EOF.

**Condition :** Sondes LSP sur la grande échelle du benchmark, notamment textes de plusieurs dizaines de MiB à mots courts.

**Coût ou dette :** O(W) TextOffset temporaires pour W débuts de mots ; préparation hors chrono des providers mais contribution à la mémoire et à l’échauffement du processus.

**Usages et visibilité :** Recherche textuelle dans src, tests, bench, examples, templates, tools et cmake. ServiceMeasurement: 4 fichiers, dont src/bench/cursiv/LanguageBench.hpp, src/bench/cursiv/LanguageBench.cpp, src/bench/cursiv/LspBench.cpp, tests/bench/bench_test.cpp ; lsp_stages: 2 fichiers, dont src/bench/cursiv/LanguageBench.hpp, src/bench/cursiv/LspBench.cpp ; measure_lsp: 1 fichiers, dont src/bench/cursiv/LanguageBench.hpp ; result_count: 1 fichiers, dont src/bench/cursiv/LspBench.cpp. Les instanciations et appels émis comme chaînes restent inclus dans cette lecture ; ce décompte ne prouve pas une utilisation externe.

**Changement proposé :** Envisager deux scans sans vecteur starts : compter W, calculer les neuf rangs voulus, puis ne convertir/conserver que ces positions lors du second passage. Comparer le coût d’un scan supplémentaire au coût mémoire avant de prioriser.

**Invariants à préserver :** Conserver exactement les rangs i*(W−1)/(count−1), le cas count==1, EOF, la détection actuelle word et les conversions UTF-16 hors chrono. Un échantillonnage aléatoire changerait le corpus de requêtes.

**Validation future — non exécutée :** Après correction : mêmes positions pour0/1/<9/>9 mots et multioctet ; vérifier mémoire de préparation et éviter d’inclure son temps dans les mesures provider.

**Preuves dans la révision auditée :**

- [src/bench/cursiv/LspBench.cpp:81](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/bench/cursiv/LspBench.cpp#L81) — Vecteur de tous les starts
- [src/bench/cursiv/LspBench.cpp:90](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/bench/cursiv/LspBench.cpp#L90) — Résultat plafonné à neuf
- [src/bench/cursiv/LspBench.cpp:95](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/bench/cursiv/LspBench.cpp#L95) — EOF ajouté séparément

<a id="finding-m-ass-session-transport"></a>
### M-ASS-SESSION-TRANSPORT — Trois décodeurs LSP et quatre attentes de réponse maintenus séparément

**factorisation · P3 · établi · confiance forte.** Périmètre principal : **ASS-020 — Sessions LSP/MCP : cadrage, corrélation et assertions d’ordre**. Périmètres associés : ASS-020.

**Frontière :** Messages envoyés à un processus → frames décodées, réponses corrélées, notifications observées et verdict de session.

**Observation :** lsp-session, lsp-order-session et mcp-tunnel-session réimplémentent accumulation de buffers, lecture Content-Length, extraction JSON et collecte des réponses. lsp-session vérifie explicitement l’absence du header ; les deux autres convertissent directement une capture optionnelle en Number. Les waitFor répètent timers et recherches, avec différences find/findLast, délais et contrôle de sortie enfant.

**Condition :** Évolution des scripts de session, ajout d’un scénario, correction d’un problème de cadrage ou de corrélation.

**Coût ou dette :** Maintenance répétée de la même couche de transport ; accumulation/recherche des anciennes réponses également répétée, sans estimation de gain runtime.

**Usages et visibilité :** Recherche textuelle dans src, tests, bench, examples, templates, tools et cmake. waitFor: 4 fichiers, dont tools/lsp-session.mjs, tools/lsp-order-session.mjs, tools/mcp-tunnel-session.mjs, tools/mcp-session.mjs ; decodeSemanticTokens: 1 fichiers, dont tools/lsp-session.mjs ; StringDecoder: 1 fichiers, dont tools/mcp-session.mjs. Les instanciations et appels émis comme chaînes restent inclus dans cette lecture ; ce décompte ne prouve pas une utilisation externe.

**Changement proposé :** Extraire un module interne de client LSP cadré et un coordonnateur de requêtes/notifications paramétrable. Centraliser encodage UTF-8, erreurs de header, arrêt enfant et timeout. Conserver un adaptateur distinct pour le JSON par ligne MCP.

**Invariants à préserver :** Ne pas fusionner les scénarios métier ni supposer que find et findLast sont équivalents pour les diagnostics répétés. Préserver ids uniques, corrélation interne/externe du tunnel, attente de snapshot le plus récent, timeout propre et support WASI de lsp-session.

**Validation future — non exécutée :** Après correction : vérifications existantes des trois scénarios, messages fragmentés/multiples et caractères UTF-8 aux frontières, terminaison enfant et timeout ; aucune session/reproducer lancée ici.

**Preuves dans la révision auditée :**

- [tools/lsp-session.mjs:123](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/tools/lsp-session.mjs#L123) — Premier décodeur avec erreur explicite de header
- [tools/lsp-order-session.mjs:20](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/tools/lsp-order-session.mjs#L20) — Copie sans même validation
- [tools/mcp-tunnel-session.mjs:26](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/tools/mcp-tunnel-session.mjs#L26) — Troisième variante
- [tools/lsp-session.mjs:147](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/tools/lsp-session.mjs#L147) — findLast pour les snapshots
- [tools/mcp-session.mjs:48](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/tools/mcp-session.mjs#L48) — Attente analogue sur protocole distinct

<a id="finding-m-api-entry-doc"></a>
### M-API-ENTRY-DOC — Le commentaire public renvoie à getEntryRule absent de Grammar

**legacy · P3 · établi · confiance forte.** Périmètre principal : **API-006 — Choix d’une règle d’entrée pour un fragment**. Périmètres associés : API-006.

**Frontière :** Texte fragmentaire et règle demandée → résultat obtenu via l’API publique du parseur

**Observation :** Parser.hpp affirme que getEntryRule vit sur le Grammar retourné, alors que Grammar ne déclare aucun membre de ce nom.

**Condition :** Lecture de l’API par un consommateur ou un agent qui cherche comment choisir une entrée.

**Coût ou dette :** Recherche inutile et risque d’écrire du code contre une API inexistante.

**Usages et visibilité :** Parser/getGrammar et Grammar sont publics ; seul le commentaire est obsolète.

**Changement proposé :** Remplacer la phrase par le contrat effectif de métadonnées et renvoyer au using entry de la grammaire.

**Invariants à préserver :** Ne pas modifier l’interface binaire ni ajouter une sélection dynamique de règle dans ce nettoyage.

**Validation future — non exécutée :** Vérification documentaire de tous les renvois getEntryRule et cohérence avec les exemples, sans exécution nécessaire.

**Preuves dans la révision auditée :**

- [src/core/cursiv/parser/Parser.hpp:24](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/parser/Parser.hpp#L24) — commentaire obsolète
- [src/core/cursiv/grammar/Grammar.hpp:17](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/grammar/Grammar.hpp#L17) — interface effective
- [docs/reference/grammar.md:103](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/docs/reference/grammar.md#L103) — using entry

<a id="finding-m-host-memory-reader"></a>
### M-HOST-MEMORY-READER — Deux lecteurs de la section d’imports Wasm ont divergé

**factorisation · P2 · établi · confiance forte.** Périmètre principal : **HOST-002 — Décodage de la mémoire importée Wasm**. Périmètres associés : HOST-002, BLD-010.

**Frontière :** Octets du module → limites et objet WebAssembly.Memory compatibles

**Observation :** L’extraction des limites mémoire est recopiée dans le runner et la page avec traitements de LEB, types et flags différents ; le commentaire de la page affirme leur identité.

**Condition :** Évolution des formes d’imports émises par la toolchain ou correction du lecteur Node.

**Coût ou dette :** Deux implémentations du format à entretenir ; les corrections doivent être propagées manuellement.

**Usages et visibilité :** Fonctions appelées avant création de Memory après compilation ; le compilateur valide le module mais ne remplace pas ce décodage spécifique.

**Changement proposé :** Extraire un lecteur pur sans dépendance Node, inclus/copié par la chaîne de distribution ; garder les exigences de partage et nombre de mémoires dans chaque hôte.

**Invariants à préserver :** Runner autonome livré dans VSIX/scaffold, URLs web, modules wasm32, cas sans mémoire importée et erreurs de compatibilité propres à chaque hôte.

**Validation future — non exécutée :** Vérifier les artefacts distribués et les imports mémoire/tables/globals/tags pris en charge après changement ; aucun module exécuté ici.

**Preuves dans la révision auditée :**

- [tools/wasi-run.mjs:171](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/tools/wasi-run.mjs#L171) — lecteur borné
- [docs/demo/server.js:10](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/docs/demo/server.js#L10) — lecteur abrégé dit identique
- [docs/demo/server.js:50](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/docs/demo/server.js#L50) — partage forcé pour la démo

<a id="finding-m-host-dirent-buffers"></a>
### M-HOST-DIRENT-BUFFERS — Les adaptateurs readdir matérialisent plusieurs buffers par entrée

**optimisation · P3 · établi · confiance forte.** Périmètre principal : **HOST-004 — Adaptation de fd_readdir sur Node Windows**. Périmètres associés : HOST-004.

**Frontière :** Descripteur de dossier et cookie → enregistrements WASI dans le buffer invité

**Observation :** Node alloue le nom et le record complet ; le navigateur alloue nom, header et record, puis recopie vers la mémoire Wasm. Les champs du header peuvent être écrits dans un scratch réutilisé et le nom copié par segments.

**Condition :** Listings longs ou appels répétés avec petits buffers invités.

**Coût ou dette :** Allocations proportionnelles aux entrées traitées et copies intermédiaires de 24 octets plus noms ; aucune fréquence mesurée.

**Usages et visibilité :** Deux adaptateurs actifs, aux politiques de listing différentes (snapshot Windows ; fichiers virtuels et . / .. navigateur).

**Changement proposé :** Supprimer d’abord header→record dans le navigateur, puis envisager un écrivain de record borné avec scratch par appel ; ne pas mutualiser les backends filesystem.

**Invariants à préserver :** Records tronqués légaux, cookie de reprise, nom UTF-8, champs initialisés à zéro, Memory.grow et durée de vie des vues.

**Validation future — non exécutée :** Contrôler statiquement puis valider les petits buffers, noms multioctets, reprises et zéro place disponible lors du correctif.

**Preuves dans la révision auditée :**

- [tools/wasi-run.mjs:113](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/tools/wasi-run.mjs#L113) — nom puis record
- [docs/demo/wasi-worker.js:74](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/docs/demo/wasi-worker.js#L74) — liste virtuelle et trois buffers

<a id="finding-m-host-vfs-directories"></a>
### M-HOST-VFS-DIRECTORIES — Chaque lookup de dossier rescane tous les fichiers virtuels

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **HOST-007 — Filesystem virtuel de la démo**. Périmètres associés : HOST-007.

**Frontière :** Chemin sous une racine et opérations de fd → données ou errno WASI

**Observation :** lookup alloue Object.keys(ctx.files), parcourt F fichiers et reconstitue names/entries à chaque lookup de dossier. Les mêmes dossiers sont consultés par stat et open.

**Condition :** Démo enrichie avec plusieurs dossiers/fichiers ou scans workspace répétés.

**Coût ou dette :** O(Q×F) pour Q consultations de dossiers, plus listes temporaires ; petit catalogue actuel peut limiter l’impact.

**Usages et visibilité :** ctx.files est fourni à chaque session et lu sans mutation ; fds garde ensuite sa propre position et ses entrées.

**Changement proposé :** Construire une table des dossiers et enfants une fois par contexte, réutiliser les données immuables lors de stat/open.

**Invariants à préserver :** Ordre des entrées, root vide, noms répertoires/fichiers, positions par descripteur et indépendance des workers.

**Validation future — non exécutée :** Comparer les listings et compter les scans pour des arborescences variées après modification.

**Preuves dans la révision auditée :**

- [docs/demo/wasi-worker.js:26](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/docs/demo/wasi-worker.js#L26) — lookup et Object.keys complet
- [docs/demo/wasi-worker.js:234](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/docs/demo/wasi-worker.js#L234) — stat appelle lookup
- [docs/demo/wasi-worker.js:245](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/docs/demo/wasi-worker.js#L245) — open appelle lookup

<a id="finding-m-host-stdin-pump"></a>
### M-HOST-STDIN-PUMP — Copie octet par octet et retries multiples dans l’anneau stdin

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **HOST-009 — Anneau partagé pour stdin dans la démo**. Périmètres associés : HOST-009.

**Frontière :** Messages encodés côté page → octets lus par fd_read du worker

**Observation :** Le transfert parcourt chaque octet avec modulo des deux côtés. Quand l’anneau est plein, chaque appel pump peut programmer son propre timer, sans identifiant unique de retry.

**Condition :** Messages volumineux et rafales pendant que le serveur consomme plus lentement que la page.

**Coût ou dette :** O(B) inévitable en octets mais opérations JS par octet ; jusqu’à un retry en attente par chaîne de pump concurrente.

**Usages et visibilité :** Écrivain page unique, lecteur WASI unique ; send et callbacks timer rejoignent le même pump.

**Changement proposé :** Copier par au plus deux segments contigus avec TypedArray.set et maintenir un seul retry programmé. Un index de tête de pending peut éviter shift si la queue devient longue.

**Invariants à préserver :** Publication atomique après copie, wrap 32 bits, zones libres/occupées, arrêt et ordre exact headers/corps.

**Validation future — non exécutée :** Valider les traversées de fin d’anneau, saturation et rafales, puis comparer le travail de copie et le nombre de timers.

**Preuves dans la révision auditée :**

- [docs/demo/server.js:115](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/docs/demo/server.js#L115) — pending et pump
- [docs/demo/server.js:122](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/docs/demo/server.js#L122) — timer sans garde
- [docs/demo/server.js:123](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/docs/demo/server.js#L123) — copie octet/modulo
- [docs/demo/wasi-worker.js:137](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/docs/demo/wasi-worker.js#L137) — copie retour octet/modulo

<a id="finding-m-host-lsp-frame-copies"></a>
### M-HOST-LSP-FRAME-COPIES — Le cadrage LSP recopie le préfixe à chaque fragment

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **HOST-010 — Sortie Wasm et cadrage LSP dans la page**. Périmètres associés : HOST-010.

**Frontière :** iovecs stdout d’un worker → messages Content-Length décodés

**Observation :** feed alloue buf.length+chunk.length et copie le buffer existant même si le corps attendu n’est pas encore complet ; chaque message complet effectue ensuite slice du suffixe.

**Condition :** Grande réponse divisée en petits fragments ou chunk contenant de nombreuses petites réponses.

**Coût ou dette :** Une réponse de B octets divisée en K fragments peut faire O(B×K) copies de préfixes ; suffixes répétés dans les batches.

**Usages et visibilité :** Chemin stdout fd1 uniquement, un état de cadrage par worker ; writeOut doit toujours détacher les données de la mémoire Wasm.

**Changement proposé :** Conserver un buffer avec capacité et curseurs de lecture/écriture ou une queue de fragments ; compacter seulement lorsque nécessaire, retenir longueur de corps après lecture du header.

**Invariants à préserver :** Limites du protocole, UTF-8, headers séparés, plusieurs messages par chunk, absence de croisement entre workers et erreurs JSON.

**Validation future — non exécutée :** Contrôler cadrage fragmenté et groupé, puis mesurer octets recopiés lors du correctif.

**Preuves dans la révision auditée :**

- [docs/demo/wasi-worker.js:148](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/docs/demo/wasi-worker.js#L148) — copie puis transfert hors mémoire partagée
- [docs/demo/server.js:97](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/docs/demo/server.js#L97) — accumulation/cadrage
- [docs/demo/server.js:109](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/docs/demo/server.js#L109) — slice du reste

<a id="finding-m-host-cancel-disposable"></a>
### M-HOST-CANCEL-DISPOSABLE — L’abonnement d’annulation survit potentiellement à la requête

**simplification · P3 · candidat · confiance moyenne.** Périmètre principal : **HOST-012 — Session JSON-RPC du client web**. Périmètres associés : HOST-012.

**Frontière :** Requête ou notification → réponse corrélée, annulation ou rejet de session

**Observation :** request ignore le résultat Disposable de onCancellationRequested. La réponse et stop libèrent waiting mais n’annulent pas cet abonnement qui capture send/id.

**Condition :** Token ou source de token maintenu après la réponse, requêtes répétées.

**Coût ou dette :** Callbacks devenus inutiles et rétention de captures jusqu’au nettoyage du token ; ampleur conditionnelle au client.

**Usages et visibilité :** L’API est optionnelle par chaînage ; seuls les tokens qui proposent un abonnement sont concernés.

**Changement proposé :** Stocker le Disposable avec resolve/reject et le libérer à la réponse ou à stop ; conserver un nettoyage idempotent.

**Invariants à préserver :** Annulation arrivée avant ou pendant la réponse, clients sans token et émission au plus utile de $/cancelRequest.

**Validation future — non exécutée :** Vérifier la durée de vie avec l’API Monaco utilisée et les trois issues réponse/erreur/arrêt.

**Preuves dans la révision auditée :**

- [docs/demo/server.js:139](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/docs/demo/server.js#L139) — résolution et suppression waiting
- [docs/demo/server.js:161](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/docs/demo/server.js#L161) — enregistrement annulation non conservé
- [docs/demo/server.js:65](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/docs/demo/server.js#L65) — arrêt

<a id="finding-m-host-document-index"></a>
### M-HOST-DOCUMENT-INDEX — Le montage du filesystem retrouve chaque index par indexOf

**simplification · P3 · établi · confiance forte.** Périmètre principal : **HOST-013 — Changement et redémarrage du langage de démo**. Périmètres associés : HOST-013.

**Frontière :** Sélection de langage → session active et ancienne session libérée

**Observation :** session.documents.map ignore son paramètre index puis recherche le même objet par session.documents.indexOf(d) pour accéder à texts.

**Condition :** D documents ouverts au démarrage.

**Coût ou dette :** D recherches de longueur croissante, O(D²), alors que les deux tableaux ont été construits dans le même ordre.

**Usages et visibilité :** session.documents est créé à partir de lang.documents et texts à la même position ; aucun réordonnancement entre les deux sites.

**Changement proposé :** Utiliser map((d, i) => ...texts[i]) au montage de files.

**Invariants à préserver :** Correspondance exacte document/texte et nom de chemin ; aucun changement du cycle de session.

**Validation future — non exécutée :** Relecture de la correspondance par index ; contrôle de plusieurs documents lors d’une validation future.

**Preuves dans la révision auditée :**

- [docs/demo/app.js:185](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/docs/demo/app.js#L185) — ordre texts
- [docs/demo/app.js:187](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/docs/demo/app.js#L187) — ordre documents
- [docs/demo/app.js:205](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/docs/demo/app.js#L205) — recherche redondante

<a id="finding-m-host-symbol-recursion"></a>
### M-HOST-SYMBOL-RECURSION — Conversion récursive des symboles documentaires dans Monaco

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **HOST-016 — Conversion des résultats de lecture Monaco**. Périmètres associés : HOST-016.

**Frontière :** Résultats LSP localisés → hover, locations, highlights, symboles et replis

**Observation :** Le lambda convert appelle récursivement map(convert) pour chaque liste children.

**Condition :** Serveur qui retourne une hiérarchie de symboles de grande profondeur H.

**Coût ou dette :** O(N) travail mais O(H) pile JS ; la profondeur du modèle contrôle la pile native du moteur JavaScript.

**Usages et visibilité :** Provider DocumentSymbol actif ; les réponses plates n’empruntent pas la récursion.

**Changement proposé :** Convertir avec une pile explicite de couples source/destination, en gardant l’ordre des enfants.

**Invariants à préserver :** Ranges, selectionRange, kind/tags/detail et distinction réponse plate/hiérarchique.

**Validation future — non exécutée :** Valider les sorties équivalentes plates et imbriquées puis vérifier une profondeur élevée lors du correctif.

**Preuves dans la révision auditée :**

- [docs/demo/lsp.js:5](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/docs/demo/lsp.js#L5) — conversions partagées
- [docs/demo/lsp.js:171](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/docs/demo/lsp.js#L171) — convert récursif des symboles

<a id="finding-m-host-extension-copies"></a>
### M-HOST-EXTENSION-COPIES — La logique d’extension est copiée dans quatre exemples

**factorisation · P2 · établi · confiance forte.** Périmètre principal : **HOST-019 — Résolution de la commande serveur VS Code**. Périmètres associés : HOST-019, HOST-020, BLD-013.

**Frontière :** Configuration, preset et fichiers disponibles → commande native ou Wasm

**Observation :** Détection Wasm, recherche de serveur, construction LanguageClient et arrêt sont quasi identiques dans arithmetics/domainmodel/requirements/statemachine.

**Condition :** Correction d’un chemin, prise en charge d’une plateforme ou changement de cycle client.

**Coût ou dette :** Quatre propagations manuelles du même correctif ; le template constitue une cinquième implémentation partiellement différente.

**Usages et visibilité :** Toutes les extensions sont actives et annoncées autonomes au packaging.

**Changement proposé :** Créer une source canonique paramétrée et la copier/générer dans les bundles autonomes ; limiter la configuration aux noms, langues et stratégie des chemins. Éviter une dépendance runtime vers le checkout parent.

**Invariants à préserver :** Priorité configuration/preset/embarqué, PATH, Windows, requirements multi-langue et clés distinctes du template.

**Validation future — non exécutée :** Vérifier les fichiers produits/bundles des quatre extensions et du scaffold et leurs résolutions de commande lors du correctif.

**Preuves dans la révision auditée :**

- [examples/domainmodel/vscode/src/extension.ts:23](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/examples/domainmodel/vscode/src/extension.ts#L23) — logique répétée
- [examples/requirements/vscode/src/extension.ts:21](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/examples/requirements/vscode/src/extension.ts#L21) — deux langages à préserver
- [examples/statemachine/vscode/src/extension.ts:159](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/examples/statemachine/vscode/src/extension.ts#L159) — même activation
- [examples/arithmetics/vscode/src/extension.ts:204](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/examples/arithmetics/vscode/src/extension.ts#L204) — même arrêt

<a id="finding-m-host-mcp-copies"></a>
### M-HOST-MCP-COPIES — Le bridge MCP a cinq sources équivalentes à maintenir

**duplication · P2 · établi · confiance forte.** Périmètre principal : **HOST-021 — Admission HTTP du bridge MCP d’extension**. Périmètres associés : HOST-021, HOST-022, BLD-014.

**Frontière :** Requête loopback → demande autorisée ou refus HTTP

**Observation :** Quatre copies sont identiques octet par octet ; le template a le même code avec commentaires adaptés. Admission et fermeture évoluent donc séparément par copier-coller.

**Condition :** Tout correctif HTTP, adaptation VS Code ou changement de transmission MCP.

**Coût ou dette :** Cinq sites à synchroniser pour la même logique, dont un intégré au scaffolder.

**Usages et visibilité :** startMcpBridge est consommé par chaque activate ; aucune copie n’est morte dans la distribution actuelle.

**Changement proposé :** Conserver une source canonique, la faire entrer dans chaque bundle/scaffold par copie ou génération traçable et contrôler sa synchronisation.

**Invariants à préserver :** Autonomie du projet généré, compatibilité API avant/après VS Code 1.101, token par session, localhost et code HTTP notification.

**Validation future — non exécutée :** Comparer les copies distribuées à leur source et valider admission/notification/fermeture après changement.

**Preuves dans la révision auditée :**

- [examples/domainmodel/vscode/src/mcp.ts:42](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/examples/domainmodel/vscode/src/mcp.ts#L42) — implémentation canonisable
- [examples/requirements/vscode/src/mcp.ts:42](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/examples/requirements/vscode/src/mcp.ts#L42) — copie
- [examples/arithmetics/vscode/src/mcp.ts:42](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/examples/arithmetics/vscode/src/mcp.ts#L42) — copie
- [examples/statemachine/vscode/src/mcp.ts:42](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/examples/statemachine/vscode/src/mcp.ts#L42) — copie
- [templates/project/vscode/src/mcp.ts:40](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/templates/project/vscode/src/mcp.ts#L40) — template équivalent

<a id="finding-m-bld-lsp-eager"></a>
### M-BLD-LSP-EAGER — La configuration core seule acquiert aussi lsp-framework

**optimisation · P2 · candidat · confiance moyenne.** Périmètre principal : **BLD-002 — Acquisition de lsp-framework et génération de son protocole**. Périmètres associés : BLD-002.

**Frontière :** Révision ou override de dépendance → sources et target lspgen disponibles

**Observation :** Le graphe CMake ne propose pas de frontière avant FetchContent_MakeAvailable(lsp_framework) ; EXCLUDE_FROM_ALL concerne la compilation et ne supprime pas cette acquisition/configuration.

**Condition :** Consommateur strictement core/parser, sans LSP, MCP ni helpers liés au protocole.

**Coût ou dette :** Téléchargement éventuel et configuration du protocole inutiles pour ce profil ; impact dépend du cache et du projet consommateur.

**Usages et visibilité :** LSP, MCP, cursivTest et cursivBench utilisent lsp ; le sous-build host peut requérir lspgen. Ne pas déplacer le return host naïvement.

**Changement proposé :** Étudier une option de packages protocole activée par défaut, ou un helper d’acquisition différée appelé par leur déclaration. Garder le comportement actuel par défaut.

**Invariants à préserver :** Targets publics existants, typechecking des headers, génération protocole cross-build et consommateurs FetchContent.

**Validation future — non exécutée :** Vérifier profils core seul et pile complète, acquisition hors ligne préremplie et sous-build host après changement.

**Preuves dans la révision auditée :**

- [src/CMakeLists.txt:65](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/CMakeLists.txt#L65) — acquisition inconditionnelle
- [src/CMakeLists.txt:79](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/CMakeLists.txt#L79) — retour host après acquisition
- [cmake/CursivGrammar.cmake:86](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/cmake/CursivGrammar.cmake#L86) — raccordement lspgen

<a id="finding-m-bld-parser-unit-count"></a>
### M-BLD-PARSER-UNIT-COUNT — Le nombre d’unités générées est défini en C++ et en CMake

**duplication · P3 · établi · confiance forte.** Périmètre principal : **BLD-005 — Dépendances et unités du parser généré**. Périmètres associés : BLD-005.

**Frontière :** Exécutable générateur modifié → fichiers déclarés puis bibliothèque Generated reconstruite

**Observation :** CommonArtifacts fixe kParserUnits=8 ; CursivGrammar énumère indépendamment RANGE 0 7 pour annoncer les outputs.

**Condition :** Changement du partitionnement du code généré.

**Coût ou dette :** Risque de désynchronisation outputs/build ; pas un coût runtime.

**Usages et visibilité :** LL et LR consomment la même constante C++ ; CMake annonce leurs cpp avant exécution du générateur.

**Changement proposé :** Créer une définition de build unique qui alimente le header et la plage CMake, ou un contrôle documentaire explicite de concordance si générer le header serait excessif.

**Invariants à préserver :** Liste déterministe des outputs à la configuration, headers utilisables par les outils hôtes, parsers fixtures regroupés et separate units.

**Validation future — non exécutée :** Vérifier les listes produites pour LL/LR en natif et cross-build lors d’un changement de nombre d’unités.

**Preuves dans la révision auditée :**

- [cmake/CursivGrammar.cmake:214](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/cmake/CursivGrammar.cmake#L214) — 0..7
- [src/engines/cursiv/shared/gen/CommonArtifacts.hpp:24](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/shared/gen/CommonArtifacts.hpp#L24) — 8

<a id="finding-m-bld-esbuild-copies"></a>
### M-BLD-ESBUILD-COPIES — La configuration esbuild est recopiée dans cinq projets

**duplication · P3 · établi · confiance forte.** Périmètre principal : **BLD-012 — Bundle et packaging des extensions VSIX**. Périmètres associés : BLD-012, BLD-014.

**Frontière :** Sources TypeScript et module Wasm → extension distribuable autonome

**Observation :** Quatre scripts de bundle sont identiques et le cinquième du template est équivalent. Les changements de target, sourcemap, nettoyage et dépendances doivent être propagés.

**Condition :** Évolution du format de bundle ou version minimale Node/VS Code.

**Coût ou dette :** Cinq sources de configuration pour la même politique ; code actif de build.

**Usages et visibilité :** Chaque package est autonome ; esbuild.mjs du template est embarqué par GenerateScaffold.

**Changement proposé :** Utiliser une source canonique recopiée par la génération et le packaging, ou contrôler les copies normalisées si conserver les fichiers autonomes reste plus simple.

**Invariants à préserver :** Autonomie hors monorepo, chemins relatifs au package, vscode externe, watch sans nettoyage destructeur des sorties en cours.

**Validation future — non exécutée :** Vérifier égalité des options effectives et contenu des bundles produits après correction.

**Preuves dans la révision auditée :**

- [examples/domainmodel/vscode/esbuild.mjs:14](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/examples/domainmodel/vscode/esbuild.mjs#L14) — configuration bundle
- [templates/project/vscode/esbuild.mjs:14](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/templates/project/vscode/esbuild.mjs#L14) — configuration équivalente
- [tools/package-vsix.sh:48](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/tools/package-vsix.sh#L48) — adaptation du manifeste de livraison

<a id="finding-m-bld-docs-setup"></a>
### M-BLD-DOCS-SETUP — La préparation et construction MkDocs ont deux définitions

**factorisation · P3 · candidat · confiance moyenne.** Périmètre principal : **BLD-017 — Publication du site et notifications après CI**. Périmètres associés : BLD-017.

**Frontière :** Résultats de jobs et événement GitHub → artefact Pages ou dispatch externe

**Observation :** Les workflows Documentation et CI recopient version Python, installation requirements-docs et mkdocs build --strict. Seul le second ajoute la démo et publie.

**Condition :** Changement des paramètres de build ou de préparation documentaire.

**Coût ou dette :** Risque de dérive entre vérification rapide et site publié ; deux constructions sur main mais leurs délais et dépendances diffèrent.

**Usages et visibilité :** docs.yml reste utile sur PR sans attendre WASI ; ci.yml utilise les modules du même workflow.

**Changement proposé :** Extraire une action locale étroite pour le socle documentaire, ou garder les deux définitions avec contrôle explicite. Ne partager un artefact que si son identité de commit et son accès interworkflow sont maîtrisés.

**Invariants à préserver :** Contrôle rapide des PR, identité des serveurs publiés, permissions Pages limitées au job de déploiement et absence de secrets dans les jobs non autorisés.

**Validation future — non exécutée :** Relire les graphes de dépendance sur PR/main/tag et comparer la commande effective de construction documentaire.

**Preuves dans la révision auditée :**

- [.github/workflows/docs.yml:30](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/.github/workflows/docs.yml#L30) — construction documentaire rapide
- [.github/workflows/ci.yml:361](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/.github/workflows/ci.yml#L361) — même base puis ajout démo
- [.github/workflows/ci.yml:368](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/.github/workflows/ci.yml#L368) — publication séparée

<a id="finding-m-exa-arith-parcours-profondeur"></a>
### M-EXA-ARITH-PARCOURS-PROFONDEUR — Les parcours d’expressions utilisent la pile native proportionnellement à l’AST

**simplification · P2 · établi · confiance forte.** Périmètre principal : **EXA-002 — Évaluation numérique, liaison des arguments et fin d’évaluation**. Périmètres associés : EXA-002, EXA-004, EXA-005, EXA-006.

**Frontière :** Expression et contexte de paramètres → valeur optionnelle ; appels, pile de définitions actives, budget et projection des évaluations du module vers la sortie CLI.

**Observation :** evaluate, select_calls, fold et walk_divisions se rappellent selon les enfants ; seul le sous-calcul du diviseur porte un budget de 64 étapes, pas le parcours documentaire ni l’interprétation CLI.

**Condition :** Expressions très imbriquées ou longues chaînes associatives.

**Coût ou dette :** O(D) pile native pour profondeur D, plusieurs dispatchs manuels de types à maintenir.

**Usages et visibilité :** Walkers privés installés par CoreModule ; interpréteur appelé par Evaluator.

**Changement proposé :** Passer à des frames explicites ; partager seulement l’énumération d’enfants lorsque les politiques concordent.

**Invariants à préserver :** Ordre gauche/droite, bindings de l’appelant, postordre du fold, sélection actuelle des appels et annulation.

**Validation future — non exécutée :** Tester ultérieurement profondeur natif/WASM, résultats, diagnostics et budget exact.

**Preuves dans la révision auditée :**

- [examples/arithmetics/src/core/arithmetics/Interpreter.cpp:85](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/examples/arithmetics/src/core/arithmetics/Interpreter.cpp#L85) — Évaluation récursive
- [examples/arithmetics/src/core/arithmetics/validation/ArithmeticsValidator.cpp:65](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/examples/arithmetics/src/core/arithmetics/validation/ArithmeticsValidator.cpp#L65) — Collecte récursive
- [examples/arithmetics/src/core/arithmetics/validation/ArithmeticsValidator.cpp:138](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/examples/arithmetics/src/core/arithmetics/validation/ArithmeticsValidator.cpp#L138) — Fold récursif
- [examples/arithmetics/src/core/arithmetics/validation/ArithmeticsValidator.cpp:198](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/examples/arithmetics/src/core/arithmetics/validation/ArithmeticsValidator.cpp#L198) — Marche documentaire récursive

<a id="finding-m-exa-arith-context-copies"></a>
### M-EXA-ARITH-CONTEXT-COPIES — Chaque appel copie l’historique actif et crée une table de paramètres

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **EXA-002 — Évaluation numérique, liaison des arguments et fin d’évaluation**. Périmètres associés : EXA-002.

**Frontière :** Expression et contexte de paramètres → valeur optionnelle ; appels, pile de définitions actives, budget et projection des évaluations du module vers la sortie CLI.

**Observation :** evaluate_call cherche linéairement dans active puis copie tout le vector dans un Context local.

**Condition :** Chaîne de D appels sans cycle, notamment évaluation sans quota.

**Coût ou dette :** O(D²) pointeurs copiés et cumulés sur les contexts actifs, plus maps de paramètres par appel.

**Usages et visibilité :** Context privé ; eval_expression/interpret_evaluations publics au langage.

**Changement proposé :** Partager une pile active push/pop et un set de présence ; garder des frames de bindings distinctes.

**Invariants à préserver :** Pas de fuite de paramètres, nettoyage sur toutes sorties nullopt, cycles et compteur commun préservés.

**Validation future — non exécutée :** Comparer appels imbriqués, ombrage et cycles ; compter copies/allocations.

**Preuves dans la révision auditée :**

- [examples/arithmetics/src/core/arithmetics/Interpreter.cpp:45](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/examples/arithmetics/src/core/arithmetics/Interpreter.cpp#L45) — Recherche linéaire
- [examples/arithmetics/src/core/arithmetics/Interpreter.cpp:52](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/examples/arithmetics/src/core/arithmetics/Interpreter.cpp#L52) — Copie de préfixe

<a id="finding-m-exa-arith-unique-buckets"></a>
### M-EXA-ARITH-UNIQUE-BUCKETS — Les checks d’unicité allouent un vector pour chaque nom unique

**optimisation · P3 · établi · confiance forte.** Périmètre principal : **EXA-003 — Unicité des noms de définitions et de paramètres**. Périmètres associés : EXA-003.

**Frontière :** Définitions d’un module ou paramètres d’une définition → groupes d’homonymes → diagnostics attachés aux déclarations concernées.

**Observation :** Les deux checks stockent map<string_view,vector<node*>> ; chaque nom unique alloue un vector d’un pointeur.

**Condition :** Beaucoup de noms distincts.

**Coût ou dette :** Une allocation par nom en plus du nœud de map.

**Usages et visibilité :** Checks Arith enregistrés ; StateMachineValidator possède déjà une entrée First/reported.

**Changement proposé :** Stocker {first,reported}, signaler first au premier doublon puis les suivants.

**Invariants à préserver :** Un diagnostic par occurrence dupliquée, propriétés et ordre attendu.

**Validation future — non exécutée :** Comparer uniques/doubles/triples et compter allocations.

**Preuves dans la révision auditée :**

- [examples/arithmetics/src/core/arithmetics/validation/ArithmeticsValidator.cpp:232](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/examples/arithmetics/src/core/arithmetics/validation/ArithmeticsValidator.cpp#L232) — Bucket paramètre
- [examples/arithmetics/src/core/arithmetics/validation/ArithmeticsValidator.cpp:247](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/examples/arithmetics/src/core/arithmetics/validation/ArithmeticsValidator.cpp#L247) — Bucket définition
- [examples/statemachine/src/core/statemachine/validation/StateMachineValidator.cpp:36](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/examples/statemachine/src/core/statemachine/validation/StateMachineValidator.cpp#L36) — Forme compacte existante

<a id="finding-m-exa-dm-package-walks"></a>
### M-EXA-DM-PACKAGE-WALKS — Les parcours de packages reconstruisent récursivement leurs préfixes

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **EXA-009 — Qualification et visibilité des types dans les packages**. Périmètres associés : EXA-009, EXA-011, EXA-013.

**Frontière :** Déclarations de types et arborescence des packages → descriptions exportées et scopes locaux, avec qualification partielle ou complète et politique de casse.

**Observation :** qualifierOf remonte et concatène une chaîne à chaque niveau, répété par type exporté ; processContainer, collect_subtree et génération descendent aussi récursivement.

**Condition :** Profondeur D, plusieurs types sous les mêmes packages.

**Coût ou dette :** O(D) pile native ; pour segments bornés une qualification recopie O(D²) caractères intermédiaires. Les alias visibles à chaque étage restent un coût intrinsèque distinct.

**Usages et visibilité :** ScopeComputation, QualifiedNameProvider, RenameProvider et generate_abstract_elements actifs.

**Changement proposé :** Parcours explicite transmettant des préfixes calculés une fois ; pour qualification isolée, collecter les segments et joindre après réservation.

**Invariants à préserver :** Alias locaux par étage, keep_text, noms pointés, rename fonctionnel et chemins Java.

**Validation future — non exécutée :** Comparer symboles/renames/chemins sur packages profonds et larges.

**Preuves dans la révision auditée :**

- [examples/domainmodel/src/core/domainmodel/references/QualifiedNameProvider.cpp:19](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/examples/domainmodel/src/core/domainmodel/references/QualifiedNameProvider.cpp#L19) — Remontée et concaténation
- [examples/domainmodel/src/core/domainmodel/references/DomainModelScopeComputation.cpp:132](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/examples/domainmodel/src/core/domainmodel/references/DomainModelScopeComputation.cpp#L132) — Descente récursive
- [examples/domainmodel/src/lsp/domainmodel/DomainModelRenameProvider.cpp:55](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/examples/domainmodel/src/lsp/domainmodel/DomainModelRenameProvider.cpp#L55) — Collecte récursive
- [examples/domainmodel/src/cli/domainmodel/Generator.cpp:129](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/examples/domainmodel/src/cli/domainmodel/Generator.cpp#L129) — Descente du générateur

<a id="finding-m-exa-dm-rename-edit-index"></a>
### M-EXA-DM-RENAME-EDIT-INDEX — Le renommage recherche chaque édition dans un vector croissant

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **EXA-011 — Renommage des packages et propagation des noms qualifiés**. Périmètres associés : EXA-011.

**Frontière :** Renommage demandé d’une déclaration → collecte des types affectés et de leurs références → ensemble d’edits de noms simples ou qualifiés.

**Observation :** Chaque référence déclenche find_if(rows) depuis le début pour remplacer l’édition de la base ou ajouter le site.

**Condition :** R références dans un document.

**Coût ou dette :** O(R²) comparaisons de positions au pire.

**Usages et visibilité :** Override installé dans DomainModel LspModule, éditions initiales créées par DefaultRenameProvider.

**Changement proposé :** Indexer localement par document la position de début vers l’index de rows, initialiser avec les éditions de base et mettre à jour aux ajouts.

**Invariants à préserver :** Une édition par site, remplacement qualifié prioritaire, ordre conservé et clé actuelle de début ; AST immuable.

**Validation future — non exécutée :** Comparer rename package/type, sites nus/qualifiés et nombreuses références.

**Preuves dans la révision auditée :**

- [examples/domainmodel/src/lsp/domainmodel/DomainModelRenameProvider.cpp:127](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/examples/domainmodel/src/lsp/domainmodel/DomainModelRenameProvider.cpp#L127) — Liste croissante
- [examples/domainmodel/src/lsp/domainmodel/DomainModelRenameProvider.cpp:129](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/examples/domainmodel/src/lsp/domainmodel/DomainModelRenameProvider.cpp#L129) — Recherche linéaire

<a id="finding-m-exa-generator-path-dup"></a>
### M-EXA-GENERATOR-PATH-DUP — Trois générateurs maintiennent la même politique de nom de sortie

**duplication · P3 · établi · confiance forte.** Périmètre principal : **EXA-013 — Correspondance packages Java et arborescence des fichiers**. Périmètres associés : EXA-013, EXA-030, EXA-031.

**Frontière :** Chemin source, destination et packages du modèle → nom racine, répertoires et fichiers Java écrits.

**Observation :** DomainModel et Requirements dupliquent sanitize_file_stem/extract_destination_and_name ; Statemachine réimplémente suppression points/tirets et valeur generated.

**Condition :** Évolution de cette convention.

**Coût ou dette :** Trois copies à synchroniser, pas de gain runtime revendiqué.

**Usages et visibilité :** Helpers internes des exemples ; projets pédagogiques indépendants.

**Changement proposé :** Partager un support d’exemples si leur distribution le permet, ou générer les helpers depuis un modèle unique ; garder suffixe et disposition propres.

**Invariants à préserver :** Noms existants, destination omise et projets copiables ; API FilePathData si consommée extérieurement.

**Validation future — non exécutée :** Vérifier stems vides/ponctuation, chemins et packaging.

**Preuves dans la révision auditée :**

- [examples/domainmodel/src/cli/domainmodel/CliUtils.cpp:11](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/examples/domainmodel/src/cli/domainmodel/CliUtils.cpp#L11) — Copie DM
- [examples/requirements/src/cli/requirements/CliUtils.cpp:11](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/examples/requirements/src/cli/requirements/CliUtils.cpp#L11) — Copie Req
- [examples/statemachine/src/cli/statemachine/Generator.cpp:21](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/examples/statemachine/src/cli/statemachine/Generator.cpp#L21) — Copie SM

<a id="finding-m-exa-req-env-union"></a>
### M-EXA-REQ-ENV-UNION — Le check d’environnements recherche plusieurs fois la même union

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **EXA-018 — Compatibilité des environnements d’un test avec ses exigences**. Périmètres associés : EXA-018.

**Frontière :** Test, environnements associés et exigences référencées → vérification de l’appartenance de chaque environnement à au moins une exigence du test.

**Observation :** Pour chaque environnement de Test, le code reboucle sur ses exigences et sur leurs environnements.

**Condition :** E environnements de test, A arêtes environnement dans les exigences.

**Coût ou dette :** O(E×A) comparaisons de pointeurs au pire.

**Usages et visibilité :** Check enregistré par install_tests_core_module ; références déjà résolues.

**Changement proposé :** Construire une union locale des Environment* puis faire les tests d’appartenance ; voie petite taille possible.

**Invariants à préserver :** Identité des cibles, nulls ignorés, diagnostic par index et ordre.

**Validation future — non exécutée :** Comparer doublons/environnements partagés et compter les comparaisons.

**Preuves dans la révision auditée :**

- [examples/requirements/src/core/requirements/validation/TestsValidator.cpp:32](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/examples/requirements/src/core/requirements/validation/TestsValidator.cpp#L32) — Boucle externe
- [examples/requirements/src/core/requirements/validation/TestsValidator.cpp:37](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/examples/requirements/src/core/requirements/validation/TestsValidator.cpp#L37) — Exigences rescannées
- [examples/requirements/src/core/requirements/validation/TestsValidator.cpp:40](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/examples/requirements/src/core/requirements/validation/TestsValidator.cpp#L40) — Boucle interne

<a id="finding-m-exa-req-html-inverse"></a>
### M-EXA-REQ-HTML-INVERSE — Le rapport rescane toutes les arêtes de tests pour chaque exigence

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **EXA-019 — Périmètre et contenu du rapport HTML de couverture**. Périmètres associés : EXA-019, EXA-031.

**Frontière :** Fichier d’exigences et dossier de travail chargé → modèles de tests retenus → contenu HTML de la matrice exigences-tests.

**Observation :** La génération boucle exigences→testModels→tests→test.requirements et rééchappe le chemin d’un TestModel à chaque ligne correspondante.

**Condition :** R exigences et A liens test→exigence.

**Coût ou dette :** O(R×A) visites au pire ; conversions répétées du même chemin.

**Usages et visibilité :** API generate_summary_file_html_content et wrapper generate_summary utilisés par CLI.

**Changement proposé :** Construire une table locale Requirement*→tests ordonnés en une passe, pré-échapper les sources par TestModel.

**Invariants à préserver :** Ordre exigences/tests, une ligne par couple malgré références doublées, nulls et provenance ; pas de cache de validation hors lifetime.

**Validation future — non exécutée :** Comparer HTML exact et compter lectures d’arêtes pour R croissant.

**Preuves dans la révision auditée :**

- [examples/requirements/src/cli/requirements/Generator.cpp:62](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/examples/requirements/src/cli/requirements/Generator.cpp#L62) — Boucle exigences
- [examples/requirements/src/cli/requirements/Generator.cpp:77](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/examples/requirements/src/cli/requirements/Generator.cpp#L77) — Recherche répétée
- [examples/requirements/src/cli/requirements/Generator.cpp:85](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/examples/requirements/src/cli/requirements/Generator.cpp#L85) — Source rééchappée

<a id="finding-m-exa-sm-transition-index"></a>
### M-EXA-SM-TRANSITION-INDEX — Le dédoublonnage quadratique des transitions est refait à chaque passe

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **EXA-023 — Sélection d’une transition par événement et génération des handlers**. Périmètres associés : EXA-023.

**Frontière :** Transitions d’un état → sélection par événement → déclarations et définitions C++ des handlers, avec comportement pour événement sans transition.

**Observation :** distinct_event_transitions utilise find sur seenEvents et est rappelé par les déclarations puis définitions.

**Condition :** T transitions nombreuses et distinctes.

**Coût ou dette :** O(T²) comparaisons, deux constructions de result/seenEvents par état.

**Usages et visibilité :** Helper anonyme avec ces deux consommateurs.

**Changement proposé :** Préparer une liste ordonnée par état avec set d’admission et la partager entre les deux passes.

**Invariants à préserver :** Première occurrence par nom, ordre des handlers et exclusion des références non résolues.

**Validation future — non exécutée :** Comparer C++ émis avec doublons/nulls et compter comparaisons/allocations.

**Preuves dans la révision auditée :**

- [examples/statemachine/src/cli/statemachine/Generator.cpp:50](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/examples/statemachine/src/cli/statemachine/Generator.cpp#L50) — Adhésion linéaire
- [examples/statemachine/src/cli/statemachine/Generator.cpp:143](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/examples/statemachine/src/cli/statemachine/Generator.cpp#L143) — Passe déclarations
- [examples/statemachine/src/cli/statemachine/Generator.cpp:157](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/examples/statemachine/src/cli/statemachine/Generator.cpp#L157) — Passe définitions

<a id="finding-m-gra-derive-key"></a>
### M-GRA-DERIVE-KEY — Sonder les formes dérivées sans copier leurs enfants sur un hit

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **GRA-004 — Descripteurs C++ vers image de grammaire**. Périmètres associés : GRA-004, GRA-005.

**Frontière :** Descripteurs de règles/expressions → nœuds ImageId

**Observation :** GrammarImage::derive construit DerivedKey{shape, vector(kidList)} avant _derived.find. Un hit paye donc une allocation/copie des enfants qui seront immédiatement détruits. Sur un miss, la liste est stockée à la fois dans la clé de map et dans kids.

**Condition :** Réécritures partageant les mêmes sous-expressions et appels répétés avec des formes dérivées de plusieurs enfants.

**Coût ou dette :** O(K) copie temporaire et allocation potentielle par lookup de K enfants, même sur hit ; doublon O(total des enfants dérivés) dans les clés retenues. La comparaison exacte reste nécessaire.

**Usages et visibilité :** Consommateurs LexReifier::make, TwinBranch et Lowerer ; _derived est interne à une image de génération, jamais concurrente.

**Changement proposé :** Commencer par une vue de clé transparente (shape + span) pour lookup, puis allouer la clé propriétaire seulement au miss. Un index vers les enfants déjà stockés pourrait supprimer la deuxième copie, mais exige de stocker des indices stables plutôt que des spans invalidés par croissance.

**Invariants à préserver :** Égalité exacte actuelle y compris kind/flags/a/m/n, identités et tailles text/text2, row/tag et enfants ordonnés ; le hash seul n'est jamais une identité.

**Validation future — non exécutée :** Comparer IDs et images obtenus sur répétition de réécritures et collisions ; mesurer allocations/hit rate de la génération à la correction, sans promettre un gain de parsing.

**Preuves dans la révision auditée :**

- [src/core/cursiv/grammar/GrammarImage.cpp:67](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/grammar/GrammarImage.cpp#L67) — Égalité complète
- [src/core/cursiv/grammar/GrammarImage.cpp:88](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/grammar/GrammarImage.cpp#L88) — Construction de clé avant find
- [src/engines/cursiv/shared/gen/LexReifier.cpp:92](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/shared/gen/LexReifier.cpp#L92) — Consommateur de derive

<a id="finding-m-gra-print-edges"></a>
### M-GRA-PRINT-EDGES — Dédupliquer les dépendances imprimées sans rescanner leur préfixe

**optimisation · P3 · établi · confiance forte.** Périmètre principal : **GRA-008 — Texte source et dump de grammaire**. Périmètres associés : GRA-008.

**Frontière :** Grammaire ou règle → représentation lisible

**Observation :** GrammarPrinter::note_edge fait ranges::find(edges,target) avant chaque insertion. Les références de la règle courante alimentent cette liste ; chaque nouvelle dépendance rescane toutes les précédentes.

**Condition :** Règle avec D dépendances distinctes et C références imprimées, ou répétitions nombreuses après construction d'une grande liste.

**Coût ou dette :** Jusqu'à O(C×D) comparaisons d'indices, O(D²) pour D premières dépendances distinctes. C'est un coût de génération/dump, pas du parseur runtime.

**Usages et visibilité :** note_edge appelé par reference_name, qui est utilisé dans l'impression des règles et listes ; les dépendances servent ensuite à ruleClosure.

**Changement proposé :** Conserver le vecteur dans l'ordre de première rencontre et ajouter une marque de visite par index de règle (génération/epoch) ou un set temporaire quand la règle est large. Réutiliser le nombre d'entrées de l'imprimante ; éviter un set par petite règle par défaut.

**Invariants à préserver :** Ordre de première apparition, `_current` hors intervalle, entrées créées pendant le parcours et fermeture des règles inchangés.

**Validation future — non exécutée :** Comparer dump et tableaux de dépendances byte à byte sur grammaires existantes et mesurer comparaisons pour grandes règles après correctif.

**Preuves dans la révision auditée :**

- [src/core/cursiv/grammar/GrammarPrinter.cpp:155](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/grammar/GrammarPrinter.cpp#L155) — Recherche linéaire
- [src/core/cursiv/grammar/GrammarPrinter.cpp:161](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/grammar/GrammarPrinter.cpp#L161) — Appel pour chaque référence

<a id="finding-m-gra-subtype-enum"></a>
### M-GRA-SUBTYPE-ENUM — Parcourir les bits présents lors de l’émission de la metadata AST

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **GRA-010 — Métadonnées AST émises**. Périmètres associés : GRA-010.

**Frontière :** Types C++ enregistrés → réflexion disponible au runtime

**Observation :** member_order effectue T² tests subtype pour compter les bases connues de chaque type. L'émission de kSubtypeTags effectue de nouveau T² tests. registry.derives contient déjà les lignes de bitsets à parcourir.

**Condition :** Nombre de types élevé avec relation de sous-typage clairsemée, surtout de nombreux types sans parent commun enregistré.

**Coût ou dette :** Deux scans O(T²) ; une énumération des bits non nuls peut viser O(T²/64 + R), R étant le nombre de relations présentes. Les autres boucles types×champs et la sortie elle-même ne disparaissent pas.

**Usages et visibilité :** Données internes du générateur ; types avec tags aliasés restent possibles, donc remplacer le compteur par le simple baseCount du tag serait incorrect.

**Changement proposé :** Créer un petit parcours des bits d'une ligne derives, réutilisé pour compter les ancêtres par destination et émettre les sous-types dans l'ordre actuel. Ne pas introduire une deuxième matrice ni reconstruire une fermeture déjà disponible.

**Invariants à préserver :** Type propre en premier, ordre stable croissant des autres types, exclusion du self dans depth, alias de tags et ordre des membres des bases virtuelles.

**Validation future — non exécutée :** Comparer arrays générés/ordre des membres sur héritage multiple et types indépendants ; mesurer coût de génération et volume émis avant toute généralisation.

**Preuves dans la révision auditée :**

- [src/engines/cursiv/shared/gen/EmitAstMetaData.cpp:30](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/shared/gen/EmitAstMetaData.cpp#L30) — Double boucle de profondeur
- [src/engines/cursiv/shared/gen/EmitAstMetaData.cpp:131](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/shared/gen/EmitAstMetaData.cpp#L131) — Double boucle des tags
- [src/engines/cursiv/shared/gen/AstRegistryBuilder.cpp:153](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/shared/gen/AstRegistryBuilder.cpp#L153) — Matrice disponible

<a id="finding-m-gra-wrapped-twice"></a>
### M-GRA-WRAPPED-TWICE — Calculer une fois le texte de grammaire replié

**duplication · P3 · établi · confiance forte.** Périmètre principal : **GRA-012 — Publication des fichiers générés**. Périmètres associés : GRA-012.

**Frontière :** Artefacts assemblés → fichiers publiés et code de retour de génération

**Observation :** emit_grammar_source appelle dump_wrapped(dump.text) pour kGrammarWrappedText ; write_common_artifacts appelle à nouveau dump_wrapped(what.dump.text) pour GRAMMAR.txt, avec les mêmes paramètres.

**Condition :** Toute génération réussie atteignant la publication du texte de grammaire.

**Coût ou dette :** Deux parcours de mise en page et deux constructions de chaîne pour le même résultat ; coût proportionnel au texte produit. Aucun impact direct sur la vitesse du parseur.

**Usages et visibilité :** Les deux consommateurs sont dans CommonArtifacts.cpp et l'helper emit_grammar_source est local au fichier ; changement limité sans API publique.

**Changement proposé :** Calculer wrapped une fois dans write_common_artifacts puis le passer à l'émetteur local et à put. Garder le texte brut pour les offsets de règle, qui ne peut être remplacé par wrapped.

**Invariants à préserver :** Octets du rendu, largeur par défaut, sous-chaînes/offsets dans kGrammarText, code de retour de la première erreur et non-réécriture des fichiers identiques.

**Validation future — non exécutée :** Comparer les deux artefacts générés avant/après et constater l'unique appel de mise en page ; utiliser les vérifications existantes de génération lors du correctif.

**Preuves dans la révision auditée :**

- [src/engines/cursiv/shared/gen/CommonArtifacts.cpp:44](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/shared/gen/CommonArtifacts.cpp#L44) — Rendu pour l’en-tête
- [src/engines/cursiv/shared/gen/CommonArtifacts.cpp:175](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/shared/gen/CommonArtifacts.cpp#L175) — Rendu pour le fichier texte

<a id="finding-m-ast-metadata-residu"></a>
### M-AST-METADATA-RESIDU — Nettoyer un include répété et le commentaire périmé de tag_of

**legacy · P3 · établi · confiance forte.** Périmètre principal : **AST-005 — Réflexion et filtrage de types**. Périmètres associés : AST-005.

**Frontière :** Type demandé → types/membres admissibles

**Observation :** AstMetaData.hpp inclut deux fois AstNode.hpp (lignes 10 et 11). Le commentaire de tag_of dit que subtypes évite un second tableau, tandis que la fonction lit types[i] et que le champ types existe. Le résidu porte sur la documentation et l'include, pas sur une table dont l'inutilité serait prouvée.

**Condition :** Toute évolution du layout de metadata ou revue de son coût mémoire.

**Coût ou dette :** Bruit de prétraitement mineur et risque de guider une future suppression sur un contrat périmé ; aucun gain runtime attendu.

**Usages et visibilité :** types est utilisé par DefaultAstReflection et EmitAstMetaData ; il doit rester dans ce nettoyage.

**Changement proposé :** Supprimer le deuxième include et réécrire le commentaire autour du lookup types[i]. Examiner séparément toute déduplication des tableaux, sans l'inclure dans ce correctif local.

**Invariants à préserver :** Identité des tags, ordre des types, ABI/layout AstMetaData et tables générées inchangés.

**Validation future — non exécutée :** Contrôler le diff et les headers générés ; aucun test comportemental nouveau nécessaire pour cette correction documentaire.

**Preuves dans la révision auditée :**

- [src/core/cursiv/syntax-tree/AstMetaData.hpp:10](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/syntax-tree/AstMetaData.hpp#L10) — Deux includes adjacents
- [src/core/cursiv/syntax-tree/AstMetaData.hpp:93](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/syntax-tree/AstMetaData.hpp#L93) — Texte ancien au-dessus de types[i]

<a id="finding-m-ast-value-as"></a>
### M-AST-VALUE-AS — Réutiliser le résultat du cast de value_as

**simplification · P3 · établi · confiance forte.** Périmètre principal : **AST-007 — Propriété d'un ParseResult et vues empruntées**. Périmètres associés : AST-007.

**Frontière :** Texte + parse → résultat puis transfert/destruction

**Observation :** value_as effectue un cast vérifié puis recommence le cast non vérifié sur succès. Pour une classe à base virtuelle, ast_cast_unchecked recherche l'offset et fait un atomic_ref::load ; cette opération peut être répétée, et n'est pas seulement une conversion syntaxique.

**Condition :** Consultation du résultat sous un type avec héritage virtuel ; coût ordinaire faible pour une base simple.

**Coût ou dette :** Deux chemins de calcul d'adresse dans le source ; suppression possible du second lookup/chargement, sans gain mesuré ni promesse sur l'optimisation du compilateur.

**Usages et visibilité :** API ParseResult::value_as appelée par de nombreux clients/tests ; ast_cast ne retourne qu'un const T* actuellement.

**Changement proposé :** Capturer le résultat de ast_cast et restituer le T* prévu par l'API (const_cast local sur le pointeur issu du membre mutable), ou ajouter un overload mutable ast_cast puis déléguer une fois. Choisir la plus petite modification compatible.

**Invariants à préserver :** T éventuellement const, null, mauvais type, tag absent, sous-objet virtuel et signature publique existante ; ne pas remplacer le contrôle par un cast inconditionnel.

**Validation future — non exécutée :** Utiliser les cas existants d'héritage simple/virtuel et résultat vide ; vérifier les types retournés à la compilation si le correctif est entrepris.

**Preuves dans la révision auditée :**

- [src/core/cursiv/syntax-tree/ParseResult.hpp:86](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/syntax-tree/ParseResult.hpp#L86) — Test suivi du second cast
- [src/core/cursiv/syntax-tree/AstNode.hpp:321](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/syntax-tree/AstNode.hpp#L321) — Lecture de l’offset virtuel
- [src/core/cursiv/syntax-tree/AstNode.hpp:764](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/syntax-tree/AstNode.hpp#L764) — Cast vérifié renvoie déjà un pointeur

<a id="finding-m-ast-list-count"></a>
### M-AST-LIST-COUNT — Éviter de recompter le même suffixe pour chaque liste AST

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **AST-009 — Écritures différées et listes AST**. Périmètres associés : AST-009.

**Frontière :** Actions différées → affectations finales

**Observation :** write réserve les listes encore sans capacité en appelant elements_from(p, field). Ce helper parcourt de p à la fin ou au prochain nest, en filtrant le field ; plusieurs listes intercalées dans une région revisitent donc des pendings identiques.

**Condition :** F champs vectoriels initialement vides reçoivent des effets dans une même région de P pendings, surtout lorsque leurs premiers éléments sont proches du début.

**Coût ou dette :** O(F×P) inspections dans le cas défavorable ; F=1 reste linéaire et est probablement courant. Le coût d'un index doit être amorti, aucun gain mesuré.

**Usages et visibilité :** Chemin interne du reducer appelé par write<F> ; la réservation ne concerne pas ActKind::assign et s'arrête aux nests.

**Changement proposé :** Étudier un comptage par field en une passe de la région, activé uniquement lorsque plusieurs listes en bénéficient, ou un petit tableau temporaire de compteurs. Garder le chemin actuel minimal pour une liste unique et éviter un hash par pending.

**Invariants à préserver :** Frontières nest, propriétaire effectif, valeurs converties pouvant échouer, ordre d'append et réservations seulement indicatives ; ne jamais compter des effets appartenant au prochain wrapper.

**Validation future — non exécutée :** Après correction, contrôler AST/listes avec plusieurs champs intercalés et nests, puis mesurer inspections et allocations sur les grammaires existantes ; rien exécuté dans cet audit.

**Preuves dans la révision auditée :**

- [src/engines/cursiv/shared/runtime/AstReducer.hpp:312](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/shared/runtime/AstReducer.hpp#L312) — Appel conditionnel du comptage
- [src/engines/cursiv/shared/runtime/AstReducer.hpp:392](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/shared/runtime/AstReducer.hpp#L392) — Boucle linéaire par field

<a id="finding-m-ast-sort-scratch"></a>
### M-AST-SORT-SCRATCH — Dimensionner le temporaire du tri AST sur les éléments réellement déplacés

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **AST-012 — Compactage des nœuds et références conservées**. Périmètres associés : AST-012.

**Frontière :** Objets temporaires construits → ensemble possédé final

**Observation :** sort_by_record réserve N pointeurs même si le vecteur est déjà trié ; stray.empty() n'est testé qu'après cette allocation. La condition actions.anyNest est globale à la grammaire et peut être vraie sans nest sur ce document.

**Condition :** Grammaire autorisant nest ou réancrage ; arbre avec beaucoup de nœuds, peu ou pas de records hors ordre.

**Coût ou dette :** Capacité demandée O(N) même pour K=0 éléments déplacés ; en Wasm la réserve consomme l'espace linéaire, et inplace_merge peut ajouter un temporaire. Pas de mesure de résidence native.

**Usages et visibilité :** Un seul appel interne au build, à préserver après prune_orphans ; les exceptions doivent laisser tous les nœuds possédés exactement une fois.

**Changement proposé :** Faire un passage non mutateur comptant les éléments hors ordre, retourner si zéro, puis réserver K et effectuer la partition stable existante. Examiner le temporaire de merge séparément ; ne pas pousser dans un vecteur susceptible de lancer après avoir écrasé la liste propriétaire.

**Invariants à préserver :** Ordre par cstNodeId, égalités/wrappers, destruction exacte une fois en cas d'exception ; garder la garantie qui motivait reserve avant mutation.

**Validation future — non exécutée :** Vérifier nœuds déjà triés, quelques wrappers et ordre fortement perturbé ; mesurer capacités et allocations à la correction, y compris Wasm.

**Preuves dans la révision auditée :**

- [src/engines/cursiv/shared/runtime/AstReducer.hpp:174](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/shared/runtime/AstReducer.hpp#L174) — Déclenchement par la grammaire
- [src/engines/cursiv/shared/runtime/AstReducer.hpp:345](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/shared/runtime/AstReducer.hpp#L345) — Allocation avant détection des strays

<a id="finding-m-cst-leaf-reserve"></a>
### M-CST-LEAF-RESERVE — Réduire la sur-réservation permanente de l’index des feuilles

**optimisation · P2 · candidat · confiance moyenne.** Périmètre principal : **CST-004 — Index paresseux des feuilles**. Périmètres associés : CST-004.

**Frontière :** Première demande de feuilles → index consultable simultanément

**Observation :** index_leaves réserve size() NodeId puis n'insère que les records de subtreeSize==1. Si L feuilles occupent N records, la capacité demandée est N alors que seules L entrées sont lues.

**Condition :** Première requête de navigation d'un gros arbre comportant beaucoup de composites, particulièrement N≫L ou sous Wasm.

**Coût ou dette :** Réserve N×sizeof(NodeId) au lieu du contenu utile L×sizeof(NodeId). Le natif ne rend pas nécessairement toutes les pages résidentes ; Wasm charge la mémoire linéaire. Deux passes coûteraient plus de lectures.

**Usages et visibilité :** Index mutable interne de Cst ; construit sur demande et partagé par les lecteurs. Pas de suppression de l'index proposée.

**Changement proposé :** Comparer un comptage exact avant allocation à une estimation bornée suivie de croissance. Garder le scan unique actuel lorsqu'il est meilleur ; privilégier un choix fondé sur compteurs N/L et allocations réelles.

**Invariants à préserver :** Ordre des feuilles, trivia et composites vides inclus, publication release/acquire, reprise après allocation échouée, aucun lecteur sous une mutation du CST.

**Validation future — non exécutée :** Mesurer N/L et réserve sur CST profonds/larges et Wasm, vérifier la construction concurrente et l'équivalence leaf_count/leaves_before après correction.

**Preuves dans la révision auditée :**

- [src/core/cursiv/syntax-tree/Cst.hpp:318](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/syntax-tree/Cst.hpp#L318) — Réserve N, insertion L sous le verrou

<a id="finding-m-cst-nth-collect"></a>
### M-CST-NTH-COLLECT — Sélectionner un résultat CST sans matérialiser la liste complète

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **CST-005 — Navigation parents, enfants et voisinage**. Périmètres associés : CST-005.

**Frontière :** NodeId ou offset → voisinage syntaxique

**Observation :** find_node_for_feature et find_node_for_keyword passent index!=0 à clamped(find_nodes_for_...). Une requête d'un seul résultat alloue donc la collection et l'inverse avant d'en choisir un élément.

**Condition :** Validation ou formatage demandant un indice non nul dans un nœud contenant beaucoup de correspondances.

**Coût ou dette :** O(M) stockage temporaire et insertion éventuelle en tête pour M résultats ; le parcours du sous-arbre reste nécessaire avec le walk inverse actuel.

**Usages et visibilité :** Consommateurs ValidationAcceptor, AbstractFormatter et tests de clamp ; API de collecte complète reste utile et doit rester.

**Changement proposé :** Ajouter une sélection interne au walk commun. Une solution simple à mémoire constante compte d'abord les matches puis sélectionne leur rang inverse ; comparer le coût des deux scans à un petit tampon borné. Éviter une deuxième implementation des règles de frontière.

**Invariants à préserver :** Index zéro, clamp au dernier si index trop grand, priorité recordFills propre au nœud, exclusion des sous-AST et ordre documentaire.

**Validation future — non exécutée :** Réutiliser les cas existants d'indices multiples/hors limite et comparer résultats de sélection à ceux de collecte ; mesurer allocations seulement lors du correctif.

**Preuves dans la révision auditée :**

- [src/core/cursiv/syntax-tree/CstUtils.cpp:103](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/syntax-tree/CstUtils.cpp#L103) — Clamp sur vecteur
- [src/core/cursiv/syntax-tree/CstUtils.cpp:137](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/syntax-tree/CstUtils.cpp#L137) — Feature collectée intégralement
- [src/core/cursiv/syntax-tree/CstUtils.cpp:160](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/syntax-tree/CstUtils.cpp#L160) — Keyword collecté intégralement

<a id="finding-m-txt-projection-oscillation"></a>
### M-TXT-PROJECTION-OSCILLATION — Borner les sauts du curseur de projection pour les spans non monotones

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **CST-007 — Projection des positions après traduction source**. Périmètres associés : API-003, CST-007, TXT-007.

**Frontière :** CST/diagnostics transformés → coordonnées du document

**Observation :** move conserve next et utilise deux while linéaires pour rejoindre la fin du span courant. Les diagnostics peuvent revenir en arrière, ce que le commentaire reconnaît ; les mêmes P pièces peuvent donc être traversées à chaque changement extrême de position.

**Condition :** R diagnostics/spans dont les fins alternent entre positions éloignées dans une traduction de P pièces ; les records en ordre monotone restent linéaires amortis.

**Coût ou dette :** O(R×P) déplacements dans ce cas structurel, contre O(R log P) avec recherche de la fin pour une séquence non monotone. Aucun workload réel ni gain mesuré établi.

**Usages et visibilité :** move est interne mais utilisé pour les diagnostics comme les records dans les deux directions. original/translated utilisent déjà upper_bound.

**Changement proposé :** Conserver le curseur rapide pour les petites avancées et basculer vers after<From>/upper_bound ou une recherche exponentielle lors des grands sauts. Alternative : chemin distinct pour diagnostics, sans les réordonner.

**Invariants à préserver :** Ordre des diagnostics, pièces vides, projection début/fin asymétrique, offsets aux frontières et chemins source→texte/texte→source.

**Validation future — non exécutée :** Vérifier équivalence des projections de spans monotones/non monotones et pièces insérées/supprimées ; instrumenter le nombre de pièces inspectées lors du correctif.

**Preuves dans la révision auditée :**

- [src/core/cursiv/syntax-tree/SourceTranslation.cpp:29](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/syntax-tree/SourceTranslation.cpp#L29) — after utilise déjà upper_bound
- [src/core/cursiv/syntax-tree/SourceTranslation.cpp:54](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/syntax-tree/SourceTranslation.cpp#L54) — Boucle et curseur réversible
- [src/core/cursiv/syntax-tree/SourceTranslation.cpp:137](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/syntax-tree/SourceTranslation.cpp#L137) — Projection des diagnostics

<a id="finding-m-txt-unicode-legacy-path"></a>
### M-TXT-UNICODE-LEGACY-PATH — Retirer l’ancien segment core du chemin de génération Unicode

**legacy · P2 · établi · confiance forte.** Périmètre principal : **TXT-003 — Génération et bornes des tables Unicode**. Périmètres associés : TXT-003.

**Frontière :** Données Unicode → tables distribuées

**Observation :** unicode_dir compose src/core/cursiv/core/grammar/unicode alors que les en-têtes livrés et inclus résident dans src/core/cursiv/grammar/unicode. C'est un chemin d'arborescence ancien encore actif dans un outil de maintenance, pas du code devenu inutile.

**Condition :** Prochaine mise à jour des tables par tools/gen_unicode_ranges.py.

**Coût ou dette :** Maintenance bloquée ou écriture dans un mauvais répertoire s'il existe ; pas un coût runtime. La génération n'a pas été lancée.

**Usages et visibilité :** Script unique cité dans les trois en-têtes générés ; leurs consommateurs utilisent tous cursiv/grammar/unicode.

**Changement proposé :** Construire unicode_dir depuis le chemin réel, idéalement pathlib avec racine du dépôt ; garder un seul répertoire de sortie partagé aux trois fichiers. Ajouter version Unicode et provenance dans les en-têtes lors d'une vraie régénération séparée.

**Invariants à préserver :** Ne pas modifier manuellement les énormes tables, garder la correspondance enum/propriétés/ranges et la même version UCD/Python.

**Validation future — non exécutée :** À la correction, vérifier la destination de chacun des trois artefacts et examiner le diff de génération ; ne pas assimiler une mise à jour Unicode à un simple correctif de chemin.

**Preuves dans la révision auditée :**

- [tools/gen_unicode_ranges.py:55](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/tools/gen_unicode_ranges.py#L55) — Segment core surnuméraire
- [src/core/cursiv/grammar/unicode/UnicodeProperty.hpp:1](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/grammar/unicode/UnicodeProperty.hpp#L1) — Emplacement effectif

<a id="finding-m-txt-update-copies"></a>
### M-TXT-UPDATE-COPIES — Éviter une copie complète du document par changement incrémental

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **TXT-006 — Application ordonnée d'éditions textuelles**. Périmètres associés : TXT-004, TXT-006, LSP-012, LSP-021.

**Frontière :** Liste d'éditions → nouveau texte/version

**Observation :** Chaque ContentChange avec range construit next(document._text), remplace le segment puis appelle setText qui efface les index. Pour le changement suivant, offsetAt reconstruit l'index de ce texte intermédiaire.

**Condition :** Notification contenant K petits changements successifs sur un document d'environ N octets.

**Coût ou dette :** O(K×N) copies de texte, auxquelles s'ajoutent les rescans des index nécessaires aux coordonnées successives ; coût temporaire de l'ancienne et de la nouvelle chaîne à chaque étape.

**Usages et visibilité :** Chemin LSP didChange ; la version n'est assignée qu'à la fin. Source des changements et document édité doivent respecter les lifetimes existants.

**Changement proposé :** Séparer la mutation interne de la chaîne de l'invalidation, et étudier un index de lignes mis à jour sur la région modifiée. Commencer par réutiliser un buffer de travail si cela préserve l'état après exception ; ne pas convertir tous les ranges avant les edits.

**Invariants à préserver :** Coordonnées de chaque changement dans le résultat du précédent, CRLF/UTF-16, ranges inversés, changements complets sans range, version finale et garantie en cas d'échec d'allocation.

**Validation future — non exécutée :** À la correction, vérifier les notifications multi-edits existantes et compter octets copiés/reconstructions de l'index ; aucune mesure effectuée ici.

**Preuves dans la révision auditée :**

- [src/core/cursiv/workspace/TextDocument.cpp:81](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/workspace/TextDocument.cpp#L81) — Boucle de changements
- [src/core/cursiv/workspace/TextDocument.cpp:90](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/workspace/TextDocument.cpp#L90) — Copie entière
- [src/core/cursiv/workspace/TextDocument.cpp:188](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/workspace/TextDocument.cpp#L188) — Invalidation de tous les index

<a id="finding-m-txt-edits-shifts"></a>
### M-TXT-EDITS-SHIFTS — Assembler le résultat des TextEdit en un passage

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **TXT-006 — Application ordonnée d'éditions textuelles**. Périmètres associés : TXT-006, MCP-019.

**Frontière :** Liste d'éditions → nouveau texte/version

**Observation :** applyEdits trie les éditions puis appelle out.replace de la fin vers le début. Chaque insertion/suppression peut déplacer le suffixe déjà présent ; appliquer en ordre inverse protège les offsets mais ne rend pas ces déplacements linéaires.

**Condition :** Grand nombre d'éditions non chevauchantes, particulièrement insertions près du début d'un gros document.

**Coût ou dette :** Tri O(K log K) déjà requis, puis jusqu'à O(K×N) octets déplacés ; une sortie assemblée par segments peut viser O(N + octets insérés) après tri.

**Usages et visibilité :** Utilisé pour formatting/tests et éditions MCP ; les clients attendent les règles existantes de stabilité et refus des chevauchements.

**Changement proposé :** Après normalisation et contrôle des ranges, calculer la taille finale avec contrôle de débordement puis append les tranches du document initial et les remplacements dans l'ordre. Garder explicitement l'ordre des insertions à la même position.

**Invariants à préserver :** Ranges clampés/inversés, insertion au début/à la fin, insertions adjacentes à un remplacement, refus des chevauchements et absence de mutation de document.

**Validation future — non exécutée :** Comparer tous les cas existants applyEdits, notamment aXYb et edits aux mêmes offsets ; mesurer les octets déplacés après correction.

**Preuves dans la révision auditée :**

- [src/core/cursiv/workspace/TextDocument.cpp:114](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/workspace/TextDocument.cpp#L114) — Tri stable puis parcours inverse
- [src/core/cursiv/workspace/TextDocument.cpp:120](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/workspace/TextDocument.cpp#L120) — Replace répété et chevauchements

<a id="finding-m-txt-trie-lookup"></a>
### M-TXT-TRIE-LOOKUP — Parcourir les segments URI par vues lors des recherches

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **TXT-010 — Parenté et chemins relatifs d'URI**. Périmètres associés : TXT-010, LSP-014, WSP-004, WSP-006.

**Frontière :** Deux URI → relation de dossier ou chemin relatif

**Observation :** getNodeImpl appelle splitSegments qui construit un vecteur de strings pour chaque URI, y compris create=false. Les méthodes publiques appellent également normalize_uri qui renvoie une string propriétaire, même si l'entrée est canonique.

**Condition :** Recherches fréquentes has/find/findAll/eraseValue sur URI longues ou profondes déjà normalisées.

**Coût ou dette :** Allocation du vecteur et copies des segments à chaque lookup, plus copie de normalisation ; petites strings peuvent rester dans SSO mais le vecteur et les copies demeurent.

**Usages et visibilité :** Descente déjà partagée const/mutable ; utilisateurs workspace gardent des URI canoniques. Une API publique normalisée sans validation ne doit pas être introduite par défaut.

**Changement proposé :** Itérer sur des string_view dans le buffer URI de l'appel et activer hash/égalité hétérogènes de children ; matérialiser la string seulement au miss créateur. Étudier ensuite un helper interne de vue NormalUri commun, sans changer les contrats publics.

**Invariants à préserver :** Segments vides de scheme://, slash terminal, URI vide, parent pointers après move, owns keys et eraseValue qui remonte la spine ; aucune vue retenue sur un buffer temporaire.

**Validation future — non exécutée :** Comparer insert/find/erase/findAll sur URI normalisées et variantes, puis compter allocations sur parcours existants ; cas multi-plateformes à conserver.

**Preuves dans la révision auditée :**

- [src/core/cursiv/utils/UriTrie.hpp:118](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/utils/UriTrie.hpp#L118) — Vector<string> intermédiaire
- [src/core/cursiv/utils/UriTrie.hpp:143](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/utils/UriTrie.hpp#L143) — Lookup après découpage

<a id="finding-m-txt-memfs-copies"></a>
### M-TXT-MEMFS-COPIES — Construire le résultat binaire directement depuis le fichier en mémoire

**simplification · P3 · établi · confiance forte.** Périmètre principal : **TXT-012 — Systèmes de fichiers mémoire et vide**. Périmètres associés : TXT-012.

**Frontière :** URI abstraite → contenu ou absence

**Observation :** readBinary appelle readFile qui retourne une std::string copiée depuis _files, puis construit vector<uint8_t> avec ses itérateurs. Le buffer temporaire est détruit aussitôt.

**Condition :** Lecture binaire d'un fichier conservé dans MemoryFileSystemProvider.

**Coût ou dette :** Deux copies de N octets au lieu d'une et une chaîne temporaire de taille N ; le vecteur de résultat reste nécessaire à l'API propriétaire.

**Usages et visibilité :** readFile et readBinary sont deux méthodes publiques du même service ; la map doit continuer de rester privée et aucune vue sortante nouvelle n'est nécessaire.

**Changement proposé :** Extraire un helper interne de recherche renvoyant const string& et gérant normalisation/erreur ; readFile en fait sa copie et readBinary construit directement le vecteur depuis la référence.

**Invariants à préserver :** Normalisation URI, même FileSystemError pour absence, ownership des résultats et exclusion de mutations concurrentes non prévues par le service.

**Validation future — non exécutée :** Comparer lectures texte/binaire de contenu avec NUL et non-UTF-8, erreurs d'URI absente, puis compter allocations pour un grand buffer lors du correctif.

**Preuves dans la révision auditée :**

- [src/core/cursiv/workspace/MemoryFileSystemProvider.cpp:49](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/workspace/MemoryFileSystemProvider.cpp#L49) — Copie intermédiaire
- [src/core/cursiv/workspace/MemoryFileSystemProvider.cpp:55](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/workspace/MemoryFileSystemProvider.cpp#L55) — Map vers string retournée

<a id="finding-m-pub-translated-cst-copy"></a>
### M-PUB-TRANSLATED-CST-COPY — Éviter la reconstruction complète du CST à chaque complétion traduite

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **PUB-001 — Contrat du parseur effacé**. Périmètres associés : PUB-001.

**Frontière :** Service Parser → résultat et grammaire consultable

**Observation :** TranslatingParser::nextFeaturesAt appelle engine_tree à chaque requête admissible. engine_tree alloue un Cst, réserve les records, initialise chacun comme leaf puis l'écrase par une copie et projette tous les spans ; il copie aussi les diagnostics.

**Condition :** Langage utilisant SourceTranslator, document effectivement modifié par la traduction et nombreuses requêtes de complétion sur le même snapshot.

**Coût ou dette :** O(N+projection) travail et stockage temporaire par requête pour N records, plus diagnostics ; initialisation des records avant overwrite ajoutée dans le source. Le coût du backend de complétion s'y ajoute.

**Usages et visibilité :** engine_tree est un helper local utilisé uniquement par TranslatingParser ; les CST initiaux et traduits partagent des identités NodeId et OwnedNodes.

**Changement proposé :** Commencer par une copie de records sans passage leaf suivi d'assignation si CstRun peut l'offrir proprement. Examiner ensuite une vue d'offsets moteur ou une copie mémorisée par snapshot uniquement si la fréquence compense la mémoire doublée ; ne pas ajouter un cache global par adresse de CST.

**Invariants à préserver :** Texte traduit vivant, ranges des diagnostics, stamps, modèle/NodeIds, rejets du caret intérieur à une pièce remplacée et invalidation sur nouveau ParseResult. Préserver l'immuabilité du CST présenté aux services.

**Validation future — non exécutée :** Comparer suggestions/span avant-après avec les tests de traduction existants ; instrumenter copies/allocations par requête lors du correctif et vérifier durée de vie des snapshots.

**Preuves dans la révision auditée :**

- [src/core/cursiv/parser/Parser.cpp:25](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/parser/Parser.cpp#L25) — Copie et reprojection complètes
- [src/core/cursiv/parser/Parser.cpp:78](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/parser/Parser.cpp#L78) — Appel pour chaque demande

<a id="finding-m-pub-expected-saturated"></a>
### M-PUB-EXPECTED-SATURATED — Arrêter l’énumération des spellings lorsque la sortie est pleine

**simplification · P3 · établi · confiance forte.** Périmètre principal : **PUB-002 — Format et classement des erreurs de parsing**. Périmètres associés : PUB-002.

**Frontière :** Diagnostic structurel → message utilisateur

**Observation :** Le test spellings.size()<limit se trouve dans la boucle mais la boucle ne s'arrête pas ; une fois la taille limite atteinte, aucun élément ultérieur ne peut être ajouté. La boucle sur d.also appelle encore add pour tous les terminaux.

**Condition :** Diagnostic dont le terminal ou les alternatives exposent plus de spellings que la limite demandée.

**Coût ou dette :** Travail résiduel proportionnel au nombre total de spellings/alternatives après saturation, avec appels virtuels terminalSpellings inutiles pour le résultat ; pas d'impact sur la mémoire de sortie bornée.

**Usages et visibilité :** Helper public, principalement utilisé pour un message à au plus quatre spellings. terminalSpellings est une lecture noexcept de grammaire.

**Changement proposé :** Sortir de la boucle interne et arrêter la boucle d.also dès saturation, en conservant le traitement prioritaire du premier terminal et le cas limit==0.

**Invariants à préserver :** Ordre, déduplication, terminal principal sans spelling qui masque les autres, limite exacte et mêmes textes produits ; ne pas réordonner les terminaux.

**Validation future — non exécutée :** Comparer sorties pour limite zéro, doublons, grande alternative et premier terminal non énumérable ; inspection du nombre d'éléments visités lors du correctif.

**Preuves dans la révision auditée :**

- [src/core/cursiv/parser/ParserErrorMessageProvider.cpp:47](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/parser/ParserErrorMessageProvider.cpp#L47) — Test de saturation sans break
- [src/core/cursiv/parser/ParserErrorMessageProvider.cpp:57](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/parser/ParserErrorMessageProvider.cpp#L57) — Parcours de toutes les alternatives

<a id="finding-m-pub-doc-token-copies"></a>
### M-PUB-DOC-TOKEN-COPIES — Réduire les copies entre fragments, tokens et nœuds documentaires

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **PUB-004 — Analyse des commentaires documentaires**. Périmètres associés : PUB-004.

**Frontière :** Texte de commentaire → blocs, tags, fragments et positions documentaires

**Observation :** Les champs name/content d'InlineTagMatch et content de Token sont des std::string. La boucle sur matches est const et copie ces champs, puis ParseContext retient const vector<Token>& et parse_line copie encore content dans le nœud final.

**Condition :** Commentaires longs ou riches en tags inline, notamment rendus à répétition dans les hovers.

**Coût ou dette :** Plusieurs copies/allocation des mêmes octets avant leur stockage propriétaire final ; pics de mémoire simultanés matches+tokens+modèle. Complexité globale linéaire dans le texte pour ces copies.

**Usages et visibilité :** Structures dans le namespace anonyme de DocComment.cpp ; DocCommentNode public reste propriétaire. Le commentaire source est vivant pendant parse_doc_comment.

**Changement proposé :** Conserver offsets/string_view dans les structures lexicales et ne copier que lors de la construction du nœud, ou rendre les tokens consommables et déplacer leur contenu. Une émission directe de tokens peut supprimer le vector<InlineTagMatch> si elle conserve les ranges et la reprise sur opener invalide.

**Invariants à préserver :** Texte du modèle indépendant de l'entrée après retour, marqueurs personnalisés, whitespace inline, ranges exacts et logique de frontière anti-rescan.

**Validation future — non exécutée :** Comparer arbres/ranges/toString/toMarkdown dans tests documentation ; mesurer nombre de copies/allocations sur gros commentaires au correctif.

**Preuves dans la révision auditée :**

- [src/core/cursiv/documentation/DocComment.cpp:145](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/documentation/DocComment.cpp#L145) — Strings intermédiaires
- [src/core/cursiv/documentation/DocComment.cpp:221](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/documentation/DocComment.cpp#L221) — Copies des matches
- [src/core/cursiv/documentation/DocComment.cpp:328](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/documentation/DocComment.cpp#L328) — Contexte const empêchant le move

<a id="finding-m-pub-json-finite-guard"></a>
### M-PUB-JSON-FINITE-GUARD — Retirer une branche de non-finitude inaccessible dans le sérialiseur privé

**mort · P3 · établi · confiance forte.** Périmètre principal : **PUB-005 — Valeurs JSON du noyau et propriété de leur contenu**. Périmètres associés : API-001, PUB-005.

**Frontière :** Valeur scalaire ou composite → JsonValue détenu et consultable

**Observation :** serialize_number retourne null si !isfinite(value), mais tous les doubles entrent par JsonValue(double), qui stocke déjà nullptr pour NaN/inf. number() renvoie une valeur et aucun setter ni accès mutable au Storage privé ne permet d'insérer un double non fini. L'appel privé depuis append_json_value ne reçoit donc que des doubles finis pour des objets utilisés conformément à leur contrat.

**Condition :** Tous les états accessibles par les constructeurs et opérations publiques de JsonValue, hors corruption mémoire/UB.

**Coût ou dette :** Branche locale redondante, coût runtime mineur ; intérêt principal : une seule définition de la normalisation des non-finis.

**Usages et visibilité :** Recherche serialize_number : définition locale et unique call à la fin du dispatch ; revue des constructors, operator= et getters dans JsonValue.cpp/.hpp.

**Changement proposé :** Supprimer la garde privée ou la remplacer par une assertion d'invariant si souhaité. Conserver la conversion NaN/inf→null au constructeur, qui est le comportement public.

**Invariants à préserver :** Sérialisation de NaN/inf comme null, nombres finis, integer distinct de double et aucune ouverture d'un accès mutable brut au Storage sans revalider l'invariant.

**Validation future — non exécutée :** Relire l'accessibilité de _value au moment du correctif ; utiliser les cas existants JsonValue avec non-finis, copie/assignment imbriqué. Pas de reproducer créé ici.

**Preuves dans la révision auditée :**

- [src/core/cursiv/services/JsonValue.cpp:64](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/services/JsonValue.cpp#L64) — Branche devenue inatteignable
- [src/core/cursiv/services/JsonValue.cpp:171](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/services/JsonValue.cpp#L171) — Normalisation unique des doubles entrants
- [src/core/cursiv/services/JsonValue.cpp:213](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/services/JsonValue.cpp#L213) — Getter double par valeur
- [src/core/cursiv/services/JsonValue.hpp:70](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/services/JsonValue.hpp#L70) — Stockage privé

<a id="finding-m-pub-doc-render-shell"></a>
### M-PUB-DOC-RENDER-SHELL — Partager l’assemblage externe des rendus de DocComment

**factorisation · P3 · établi · confiance forte.** Périmètre principal : **PUB-011 — Rendu Markdown des commentaires documentaires**. Périmètres associés : PUB-011.

**Frontière :** Documentation structurée et hooks de rendu → Markdown affichable

**Observation :** Les deux fonctions de DocComment diffèrent uniquement par l'appel element.toString()/toMarkdown(options). Elles répètent gestion du premier élément, fill_newlines, accumulation et trim.

**Condition :** Évolution future de la séparation des paragraphes ou de la gestion du rendu vide.

**Coût ou dette :** Deux petits blocs à maintenir en parallèle ; aucun gain runtime significatif revendiqué.

**Usages et visibilité :** Fonctions publiques conservées ; un helper anonyme local peut être paramétré par le renderer, comme render_inlines l'est déjà.

**Changement proposé :** Extraire render_elements(elements, render) retournant la string finale et garder les deux wrappers explicites. Ne pas généraliser toute la hiérarchie DocCommentNode ou déplacer les hooks.

**Invariants à préserver :** Rendus vides, sauts de ligne déjà présents, trim ASCII, options Markdown et ordre d'appel des hooks.

**Validation future — non exécutée :** Comparer les snapshots existants toString/toMarkdown pour paragraphes vides, blocs de code et tags ; une vérification ciblée suffit au correctif.

**Preuves dans la révision auditée :**

- [src/core/cursiv/documentation/DocComment.cpp:645](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/documentation/DocComment.cpp#L645) — Premier bloc
- [src/core/cursiv/documentation/DocComment.cpp:657](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/documentation/DocComment.cpp#L657) — Deuxième bloc

<a id="finding-m-ll-operator-active"></a>
### M-LL-OPERATOR-ACTIVE — Indexer l’appartenance des règles actives lors du déroulement d’opérateurs

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **LL-005 — Abaissement des opérateurs et niveaux de précédence**. Périmètres associés : LL-005.

**Frontière :** Déclarations infix et alias textuels → opérateurs abaissés, primaire et niveaux.

**Observation :** lay_operator teste chaque référence avec ranges::count(reading,n.a), puis pousse/poppe le même vecteur. La liste contient un chemin actif ; sa recherche linéaire est refaite à chaque niveau.

**Condition :** Opérateur passant par une longue chaîne de data rules/aliases ou des séquences et choix profonds.

**Coût ou dette :** Pour une chaîne de d références toutes distinctes : O(d²) comparaisons d’appartenance, indépendamment de l’émission utile O(d).

**Usages et visibilité :** Recherche textuelle dans src, tests, bench, examples, templates, tools et cmake. lay_operator: 2 fichiers, dont src/engines/cursiv/ll/gen/LlLowerer.hpp, src/engines/cursiv/ll/gen/LlLowerer.cpp ; emit_op: 4 fichiers, dont src/engines/cursiv/lr/gen/LrLowerer.hpp, src/engines/cursiv/lr/gen/LrLowerDefine.cpp, src/engines/cursiv/ll/gen/LlLowerer.hpp, src/engines/cursiv/ll/gen/LlLowerer.cpp. Les instanciations et appels émis comme chaînes restent inclus dans cette lecture ; ce décompte ne prouve pas une utilisation externe.

**Changement proposé :** Conserver reading pour l’ordre et ajouter un marquage actif indexé par rule ID, ou un set local si IDs peu denses ; sortir les marques au pop. En second temps seulement, convertir le parcours en pile explicite.

**Invariants à préserver :** Détecter les cycles sur le chemin actif et non sur tout le graphe visité ; conserver conversion de data rules et règle nommée par l’opérateur.

**Validation future — non exécutée :** Vérifier ultérieurement les opérateurs avec aliases partagés, cycles, guards et conversions ; compter probes plutôt que revendiquer un gain CPU.

**Preuves dans la révision auditée :**

- [src/engines/cursiv/ll/gen/LlLowerer.cpp:65](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/ll/gen/LlLowerer.cpp#L65) — count puis push/pop

<a id="finding-m-ll-shape-graph"></a>
### M-LL-SHAPE-GRAPH — Construire une seule fois le graphe des analyses de forme LL

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **LL-006 — Fixpoints de nullabilité physique et logique**. Périmètres associés : LL-006, LL-008, LL-049.

**Frontière :** Graphe abaissé → deux lectures de la nullabilité ; propagation sur dépendances et traitement de fin d’entrée.

**Observation :** settle_shapes recrée users, recalcule StrongComponents et stable_sort(order) à chaque invocation. La construction l’appelle pour deux nullabilités, FIRST, productivité et shape des gaps, alors que la dépendance structurelle positions/calls reste commune dans la phase stable.

**Condition :** Génération d’une grammaire contenant beaucoup de positions et règles ; coût payé même si les faits convergent en peu de visites.

**Coût ou dette :** À chaque passe : allocations de V listes + E insertions, SCC O(V+E), tri O(V log V). Les compteurs existants incrémentent les évaluations update mais pas cette préparation.

**Usages et visibilité :** Recherche textuelle dans src, tests, bench, examples, templates, tools et cmake. settle_shapes: 1 fichiers, dont src/engines/cursiv/ll/gen/ll_build.cpp ; settle_nullable: 1 fichiers, dont src/engines/cursiv/ll/gen/ll_build.cpp. Les instanciations et appels émis comme chaînes restent inclus dans cette lecture ; ce décompte ne prouve pas une utilisation externe.

**Changement proposé :** Introduire un objet local de dépendances (users et ordre) dans ll_build_program ; le passer aux seules analyses dont le cfg structurel n’a pas changé. Le détruire avant émission. Éviter un cache global.

**Invariants à préserver :** Séparer les deux sémantiques de nullabilité ; vérifier si LexAlphabet::terminals ajoute des positions avant de réutiliser le graphe pour gaps. Maintenir l’ordre SCC et le réveil unique.

**Validation future — non exécutée :** Après correctif, comparer les tables générées et les refus, instrumenter nombre de graphes/SCC et allocations sur GeneratorGrowth. Rien exécuté ici.

**Preuves dans la révision auditée :**

- [src/engines/cursiv/ll/gen/ll_build.cpp:66](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/ll/gen/ll_build.cpp#L66) — construction répétée
- [src/engines/cursiv/ll/gen/ll_build.cpp:2235](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/ll/gen/ll_build.cpp#L2235) — séquence appels

<a id="finding-m-ll-return-closure"></a>
### M-LL-RETURN-CLOSURE — Mutualiser les fermetures epsilon des classes de retour

**optimisation · P2 · candidat · confiance moyenne.** Périmètre principal : **LL-010 — Classes de retour et états après une position**. Périmètres associés : LL-010.

**Frontière :** ATN construit → classes équivalentes, marqueurs tail et états de continuation.

**Observation :** build_atn lance une marche depuis chaque état marqué returns, remet les marques visited à zéro, trie stops puis interne le résultat. Deux retours qui rejoignent le même suffixe répètent sa traversée.

**Condition :** ATN avec nombreux sites de retour partageant de longs suffixes epsilon.

**Coût ou dette :** Coût O(R·(Vε+Eε)) dans le cas de fermetures très chevauchantes, plus tri des stops ; R est le nombre de retours parcourus.

**Usages et visibilité :** Recherche textuelle dans src, tests, bench, examples, templates, tools et cmake. returnClass: 6 fichiers, dont src/engines/cursiv/ll/gen/ll_emit_table.cpp, src/engines/cursiv/ll/gen/LlBuiltProgram.hpp, src/engines/cursiv/ll/gen/ll_build.cpp, src/engines/cursiv/ll/table/LlAtnView.hpp, src/engines/cursiv/ll/runtime/LlEpisodeCache.hpp ; settle_after_states: 1 fichiers, dont src/engines/cursiv/ll/gen/ll_build.cpp. Les instanciations et appels émis comme chaînes restent inclus dans cette lecture ; ce décompte ne prouve pas une utilisation externe.

**Changement proposé :** Étudier une condensation SCC du sous-graphe epsilon puis propagation/interning des ensembles de stops par composante ; garder une version simple si les tables usuelles sont petites.

**Invariants à préserver :** Un état avec arête non epsilon est un stop même s’il a aussi des sorties epsilon ; conserver kLlTailReturn et l’égalité exacte des ensembles.

**Validation future — non exécutée :** Comparer returnClass, taille des tables et coût mémoire de sets propagés ; une fermeture mémoïsée peut coûter plus de mémoire que les marches actuelles.

**Preuves dans la révision auditée :**

- [src/engines/cursiv/ll/gen/ll_build.cpp:1372](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/ll/gen/ll_build.cpp#L1372) — boucle par retour

<a id="finding-m-ll-dfa-settles"></a>
### M-LL-DFA-SETTLES — Remplacer le fixpoint à scans complets du DFA par une file inverse

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **LL-011 — Classification des décisions et construction du DFA de regard**. Périmètres associés : LL-011, LL-022.

**Frontière :** FIRST/ATN d’une décision → single, staged ou adaptive ; exploration et abandon de la voie statique.

**Observation :** La boucle grew parcourt tous les états et leurs arêtes pour propager settles. Un prédécesseur qui vient avant son successeur dans l’ordre de stockage peut attendre une passe supplémentaire.

**Condition :** Exploration statique d’une décision avec longues chaînes de prédécesseurs ou cycles et sorties vers une décision tranchée.

**Coût ou dette :** Jusqu’à O(V·(V+E)) pour une propagation qui gagne un niveau par passe ; exploration bornée mais répétée pour les décisions.

**Usages et visibilité :** Recherche textuelle dans src, tests, bench, examples, templates, tools et cmake. explore_decision: 1 fichiers, dont src/engines/cursiv/ll/gen/ll_build.cpp ; settle_decisions: 1 fichiers, dont src/engines/cursiv/ll/gen/ll_build.cpp. Les instanciations et appels émis comme chaînes restent inclus dans cette lecture ; ce décompte ne prouve pas une utilisation externe.

**Changement proposé :** Construire les prédécesseurs des transitions hors hand-over, ensemencer les états alt!=kNoRow et propager une fois par arête.

**Invariants à préserver :** Ne pas marquer comme settles un état uniquement par sortie hand-over ; conserver les tokens et le remplacement des cibles sans issue.

**Validation future — non exécutée :** Comparer DFA exact ou comportements et nombre de lectures d’arêtes sur chaînes inversées ; vérifier les plafonds/fallback inchangés.

**Preuves dans la révision auditée :**

- [src/engines/cursiv/ll/gen/ll_build.cpp:1517](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/ll/gen/ll_build.cpp#L1517) — fixpoint scans complets

<a id="finding-m-ll-plain-entry"></a>
### M-LL-PLAIN-ENTRY — Éviter LlMachine lorsque toute l’entrée est déjà classée plain

**simplification · P2 · établi · confiance forte.** Périmètre principal : **LL-013 — Frames, appels suspendus et profondeur plain**. Périmètres associés : LL-013, LL-019, LL-020.

**Frontière :** Règles finalisées → fonctions plain ou steps suspendus, layouts des frames et labels de reprise.

**Observation :** L’analyse plain est transitive et produit une fonction sans frame, mais parse_entry crée toujours LlMachine et machine.run. Le premier push alloue au moins 4096 octets même pour cette entrée sans suspension.

**Condition :** Grammaire dont la règle d’entrée est classée plain, sans climb ni chaîne native au-delà du plafond.

**Coût ou dette :** Allocation/libération d’un bloc de frames et tour de dispatch évitables par parse ; taille du bloc initial prouvée à 4096 octets, aucun gain temporel mesuré.

**Usages et visibilité :** Recherche textuelle dans src, tests, bench, examples, templates, tools et cmake. ll_plain_depths: 3 fichiers, dont src/engines/cursiv/ll/gen/ll_emit_parser.hpp, src/engines/cursiv/ll/gen/ll_emit_parser.cpp, tests/engines/ll/LlGeneration_test.cpp ; parse_entry: 2 fichiers, dont src/engines/cursiv/ll/gen/ll_emit_parser.cpp, tests/engines/ll/LlGeneration_test.cpp ; LlMachine: 5 fichiers, dont src/engines/cursiv/ll/gen/ll_emit_parser.cpp, src/engines/cursiv/ll/runtime/LlContext.hpp, src/engines/cursiv/ll/runtime/LlMachine.hpp, tests/engines/ll/LlGeneration_test.cpp, tests/engines/ll/LlEngine_test.cpp. Les instanciations et appels émis comme chaînes restent inclus dans cette lecture ; ce décompte ne prouve pas une utilisation externe.

**Changement proposé :** Émettre parse_entry appelant directement s<entry>(c) lorsque memo.plain[entry]!=0 ; garder la machine pour les autres entrées. Examiner séparément si c.stack.reserve(64) reste nécessaire.

**Invariants à préserver :** Plain doit continuer à garantir l’absence de suspension et respecter kLlPlainDepth ; conserver c.give, tracing/records, conditions, réparations et annulation.

**Validation future — non exécutée :** Valider après correctif les grammaires plain avec repairs et conditions ; vérifier qu’aucun helper accessible ne lit c.machine dans ce chemin ; comparer allocations et sources générées.

**Preuves dans la révision auditée :**

- [src/engines/cursiv/ll/gen/ll_emit_parser.cpp:1424](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/ll/gen/ll_emit_parser.cpp#L1424) — preuve classification transitive
- [src/engines/cursiv/ll/gen/ll_emit_parser.cpp:1607](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/ll/gen/ll_emit_parser.cpp#L1607) — machine inconditionnelle
- [src/engines/cursiv/ll/runtime/LlMachine.hpp:78](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/ll/runtime/LlMachine.hpp#L78) — 4096 init

<a id="finding-m-ll-entry-provenance"></a>
### M-LL-ENTRY-PROVENANCE — Éviter la reconstruction répétée de toute la provenance des chaînes AST

**optimisation · P2 · candidat · confiance moyenne.** Périmètre principal : **LL-017 — Stamps et correspondance records–règles–features**. Périmètres associés : LL-017.

**Frontière :** Positions de grammaire → stamps compacts et tables d’entrée AST consommables depuis le CST.

**Observation :** Pour chaque chaîne e, ll_entry_runs remonte origin jusqu’à la base et crée un vecteur par run ; ll_entry_halves réaplatit tous les runs sauf le dernier. Des chaînes formant des préfixes successifs répètent les mêmes parcours et copies.

**Condition :** Fusions successives créant de nombreuses chaînes avec ancêtres communs.

**Coût ou dette :** Si C chaînes ont des profondeurs 1..C : O(C²) segments remontés/copiés, auquel s’ajoute le volume d’actions. Une partie du volume final peut lui-même être nécessaire.

**Usages et visibilité :** Recherche textuelle dans src, tests, bench, examples, templates, tools et cmake. LlStampPlan: 4 fichiers, dont src/engines/cursiv/ll/gen/LlStampPlan.hpp, src/engines/cursiv/ll/gen/LlFinalProgram.hpp, src/engines/cursiv/ll/gen/ll_emit_table.cpp, src/engines/cursiv/ll/gen/ll_emit_parser.cpp ; ll_entry_runs: 1 fichiers, dont src/engines/cursiv/ll/gen/ll_entry_rows.cpp ; ll_entry_halves: 1 fichiers, dont src/engines/cursiv/ll/gen/ll_entry_rows.cpp. Les instanciations et appels émis comme chaînes restent inclus dans cette lecture ; ce décompte ne prouve pas une utilisation externe.

**Changement proposé :** Distinguer runs immuables partagés et vues de provenance ; mémoïser les propriétés makes/opens par run ; assembler seulement les actions exigées par RecordGen/AstEntryTable au lieu de reconstruire raw partout.

**Invariants à préserver :** Ne pas aplatir les frontières de scope qui protègent caller vs child, notamment adopting de même famille et nest. Garder doc par chaîne.

**Validation future — non exécutée :** Comparer callbacks/RecordGen et AST sur fusions profondes ; compter segments parcourus, allocations et volume final pour séparer travail évitable et sortie nécessaire.

**Preuves dans la révision auditée :**

- [src/engines/cursiv/ll/gen/ll_entry_rows.cpp:16](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/ll/gen/ll_entry_rows.cpp#L16) — remontée complète
- [src/engines/cursiv/ll/gen/ll_entry_rows.cpp:34](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/ll/gen/ll_entry_rows.cpp#L34) — réaplatissement
- [src/engines/cursiv/ll/gen/ll_entry_rows.cpp:68](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/ll/gen/ll_entry_rows.cpp#L68) — toutes les chaînes

<a id="finding-m-ll-tls-room"></a>
### M-LL-TLS-ROOM — Borner la capacité conservée par les buffers du moteur d’épisode TLS

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **LL-026 — Sessions, réentrance et ressources TLS**. Périmètres associés : LL-026, LL-052.

**Frontière :** Entrée de session → activation TLS, allocation paresseuse, abandon d’épisode et restauration de la session précédente.

**Observation :** L’objet thread_local LlEpisode<T> possède positions, spare, histories, readings et plusieurs vecteurs imbriqués. release ne vide pas leurs capacités ; le prochain reset les clear. Les mémos LlEpisodeCache meurent à la fin du parse, ce qui ne libère pas cette mémoire scratch.

**Condition :** Un parse exceptionnellement ambigu sur un thread durable, puis petits documents ; plusieurs langages T sur plusieurs workers multiplient les maxima indépendants.

**Coût ou dette :** Rétention au maximum observé de nombreux buffers par couple thread/table. Pas de contention mutex ici : c’est une empreinte mémoire TLS, différente des caches partagés.

**Usages et visibilité :** Recherche textuelle dans src, tests, bench, examples, templates, tools et cmake. ll_episode_engine: 4 fichiers, dont src/engines/cursiv/ll/runtime/LlEpisode.hpp, src/engines/cursiv/ll/runtime/LlPredictionSession.hpp, src/engines/cursiv/ll/runtime/ll_predictor.hpp, tests/engines/ll/LlConditions_test.cpp ; release: 32 fichiers, dont src/mcp/cursiv/DefaultMcpService.cpp, src/engines/cursiv/shared/gen/HeadMatcher.hpp, src/engines/cursiv/ll/table/LlSimulation.hpp, src/engines/cursiv/ll/runtime/LlEpisode.hpp, src/engines/cursiv/ll/runtime/LlHistory.hpp ; LlPredictionSession: 6 fichiers, dont src/engines/cursiv/ll/runtime/LlLanguageParser.hpp, src/engines/cursiv/ll/runtime/LlPredictionSession.hpp, tests/engines/ll/LlSwiftWidth_test.cpp, tests/engines/ll/LlLateWidth_test.cpp, tests/engines/ll/LlEngine_test.cpp. Les instanciations et appels émis comme chaînes restent inclus dans cette lecture ; ce décompte ne prouve pas une utilisation externe.

**Changement proposé :** Ajouter une politique locale de trim à la sortie de session lorsque aucun épisode ne reste détenu, avec seuils de capacité et traitement des vecteurs imbriqués. Conserver les petites capacités réutilisables.

**Invariants à préserver :** Ne jamais libérer un walk prêté actif ni invalider une session extérieure lors de réentrance ; séparer trim scratch et cache sémantique.

**Validation future — non exécutée :** Mesurer après correctif gros parse puis petits parses sur plusieurs workers/langages et réentrance ; distinguer capacité libérée et RSS restituée par l’allocateur.

**Preuves dans la révision auditée :**

- [src/engines/cursiv/ll/runtime/LlEpisode.hpp:220](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/ll/runtime/LlEpisode.hpp#L220) — release sans trim des vecteurs
- [src/engines/cursiv/ll/runtime/LlEpisode.hpp:1593](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/ll/runtime/LlEpisode.hpp#L1593) — stockages durables
- [src/engines/cursiv/ll/runtime/LlEpisode.hpp:1675](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/ll/runtime/LlEpisode.hpp#L1675) — TLS
- [src/engines/cursiv/ll/runtime/LlPredictionSession.hpp:21](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/ll/runtime/LlPredictionSession.hpp#L21) — point de sortie session

<a id="finding-m-ll-cell-membership"></a>
### M-LL-CELL-MEMBERSHIP — Indexer les piles déjà admises dans une cellule d’appels partagés

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **LL-030 — Cellules de piles et partage d’appels**. Périmètres associés : LL-030, LL-031.

**Frontière :** Branches ayant des retours communs → cellules de membres et formes canoniques de piles.

**Observation :** call recherche une pile dans tous les membres de la cellule via holds avant de l’ajouter. Le partage évite de rejouer le corps, mais l’admission de m piles distinctes lit 0+1+…+(m−1) membres.

**Condition :** Nombreux chemins d’une clôture vers une même règle partagée avec stacks différents.

**Coût ou dette :** O(m²) comparaisons de stack pour remplir une cellule ; sort/seal ultérieur ne réduit pas ce coût d’admission.

**Usages et visibilité :** Recherche textuelle dans src, tests, bench, examples, templates, tools et cmake. LlStackSet: 3 fichiers, dont src/engines/cursiv/ll/runtime/LlEpisodeSim.hpp, src/engines/cursiv/ll/runtime/LlStackSet.hpp, tests/engines/ll/LlLentStands_test.cpp ; member_related: 2 fichiers, dont src/engines/cursiv/ll/runtime/LlEpisodeSim.hpp, tests/engines/ll/LlLentStands_test.cpp. Les instanciations et appels émis comme chaînes restent inclus dans cette lecture ; ce décompte ne prouve pas une utilisation externe.

**Changement proposé :** Conserver members dans l’ordre actuel pour l’histoire préférée et ajouter un index (cell,stack) incrémental ; activer au-delà d’un petit seuil si le cas commun est minuscule.

**Invariants à préserver :** L’appartenance actuelle porte sur stack seulement, pas arm/lex ; préserver le premier membre et son histoire, clear/open et les dépendances entre cellules.

**Validation future — non exécutée :** Vérifier les lectures ambiguës, lexicales et les joins de cellules ; compter probes selon m et allocations de l’index avant adoption.

**Preuves dans la révision auditée :**

- [src/engines/cursiv/ll/runtime/LlEpisodeSim.hpp:794](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/ll/runtime/LlEpisodeSim.hpp#L794) — site chaud
- [src/engines/cursiv/ll/runtime/LlStackSet.hpp:62](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/ll/runtime/LlStackSet.hpp#L62) — scan linéaire

<a id="finding-m-ll-frontier-insert"></a>
### M-LL-FRONTIER-INSERT — Réduire les déplacements du front de positions trié

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **LL-032 — Gestion et fusion du front de positions d’épisode**. Périmètres associés : LL-032.

**Frontière :** Lectures à placer → insertion, extraction et regroupement par position → prochain groupe à traiter.

**Observation :** insert utilise lower_bound sur vector puis vector::insert pour chaque nouvelle position. Les champs d’une Position contiennent un vecteur de threads ; les déplacements du suffixe restent nécessaires même si les buffers internes sont déplacés sans copier les threads.

**Condition :** Épisode contenant simultanément de nombreuses positions distinctes, enrichies dans un ordre qui n’est pas celui du tri.

**Coût ou dette :** Pour F insertions défavorables : O(F²) déplacements de Position ; recherche de place O(log F). Le cas où les positions arrivent en ordre croissant coûte bien moins.

**Usages et visibilité :** Recherche textuelle dans src, tests, bench, examples, templates, tools et cmake. ll_thread_set_key: 1 fichiers, dont src/engines/cursiv/ll/runtime/LlEpisode.hpp ; unite_same_states: 1 fichiers, dont src/engines/cursiv/ll/runtime/LlEpisode.hpp. Les instanciations et appels émis comme chaînes restent inclus dans cette lecture ; ce décompte ne prouve pas une utilisation externe.

**Changement proposé :** Mesurer taille/ordre du front puis envisager heap de clés + index de positions, ou insertion groupée triée puis fusion ; conserver vector si F reste petit.

**Invariants à préserver :** Ordre exact de Key incluant offset, indentation, held et lull ; fusion des positions égales ; durée des références renvoyées par insert ; aucune fusion sur le seul offset.

**Validation future — non exécutée :** Contrôler résultats et plans sur lexical ambigu/indentation ; attribuer déplacements et F maximal, sans inventer de gain.

**Preuves dans la révision auditée :**

- [src/engines/cursiv/ll/runtime/LlEpisode.hpp:486](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/ll/runtime/LlEpisode.hpp#L486) — clé
- [src/engines/cursiv/ll/runtime/LlEpisode.hpp:493](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/ll/runtime/LlEpisode.hpp#L493) — insertion triée

<a id="finding-m-ll-memo-unreachable-tail"></a>
### M-LL-MEMO-UNREACHABLE-TAIL — Limiter le stockage des entrées de mémo devenues hors horizon de consultation

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **LL-033 — Clés, égalité et rétention du mémo d’épisode**. Périmètres associés : LL-033.

**Frontière :** Question de prédiction → recherche exacte, copie du résultat et conservation des plans.

**Observation :** Chaque add append une ligne et tout son plan. find_at ne consulte que les 16 premières lignes d’une chaîne ; ajouter une 17e rend la plus ancienne inatteignable par ce lookup, sans récupérer sa ligne ni ses plans. Cela concerne chaque hash, y compris les hashes liés à EOF.

**Condition :** Plus de 16 enregistrements successifs dans une même chaîne au cours d’un long parse.

**Coût ou dette :** Volume O(nombre total de lignes + somme des plans) jusqu’à la destruction de session, alors que les chaînes ne consultent qu’un suffixe récent ; le coût memcmp peut encore être proportionnel aux longueurs des candidats visités.

**Usages et visibilité :** Recherche textuelle dans src, tests, bench, examples, templates, tools et cmake. LlEpisodeMemo: 4 fichiers, dont src/engines/cursiv/ll/runtime/LlEpisode.hpp, src/engines/cursiv/ll/runtime/LlEpisodeCache.hpp, src/engines/cursiv/ll/runtime/LlEpisodeMemo.hpp, tests/engines/ll/LlEngine_test.cpp ; ll_remembered_hash: 2 fichiers, dont src/engines/cursiv/ll/runtime/LlEpisode.hpp, src/engines/cursiv/ll/runtime/LlEpisodeMemo.hpp. Les instanciations et appels émis comme chaînes restent inclus dans cette lecture ; ce décompte ne prouve pas une utilisation externe.

**Changement proposé :** Étudier une structure bornée par chaîne ou une compaction périodique des lignes/plans conservés après définition du contrat d’invalidation. Ne pas présenter un simple erase de _rows comme sûr : les indices next/plan et la comparaison d’ancienneté doivent être refaits. LlRemembered est copiable avec plan/count publics ; find puis lay sur une copie ancienne reste aujourd’hui possible tant que le mémo vit. Une réduction de stockage doit préserver ce droit ou documenter/valider son changement, même si les appels internes find→lay sont immédiats.

**Invariants à préserver :** Préférence de ligne la plus récente entre les chaînes plain/ended, offsets relatifs, égalité exacte, durée des pointeurs ET des copies LlRemembered utilisées avec lay, rehash et sessions réentrantes. L’inaccessibilité par find ne prouve pas l’absence de copies conservées par un consommateur du header.

**Validation future — non exécutée :** Comparer hits/plans sur collision de hash et dépendance EOF ; vérifier que les lignes retirées étaient hors kAsked ET qu’aucun handle/copieur autorisé ne peut encore consulter leurs plans. Examiner la visibilité du header et le contrat find/lay ; suivre octets de plans gardés, pas seulement nombre de lookups.

**Preuves dans la révision auditée :**

- [src/engines/cursiv/ll/runtime/LlEpisodeMemo.hpp:86](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/ll/runtime/LlEpisodeMemo.hpp#L86) — append
- [src/engines/cursiv/ll/runtime/LlEpisodeMemo.hpp:122](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/ll/runtime/LlEpisodeMemo.hpp#L122) — horizon 16
- [src/engines/cursiv/ll/runtime/LlEpisodeMemo.hpp:78](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/ll/runtime/LlEpisodeMemo.hpp#L78) — préférence plain/ended
- [src/engines/cursiv/ll/runtime/LlEpisode.hpp:435](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/ll/runtime/LlEpisode.hpp#L435) — Consommateur interne find puis lay immédiat ; ne prouve pas l’absence de copies externes
- [src/CMakeLists.txt:30](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/CMakeLists.txt#L30) — Include root engines exposé par INTERFACE

<a id="finding-m-ll-future-projection"></a>
### M-LL-FUTURE-PROJECTION — Centraliser les champs scalaires du futur pour hash et égalité

**factorisation · P3 · établi · confiance forte.** Périmètre principal : **LL-036 — Identité d’un futur et admission des candidats**. Périmètres associés : LL-036.

**Frontière :** État de lecture réparée → clé de futur, comparaison exacte et admission dans la recherche.

**Observation :** ll_search_future et ll_search_same_future énumèrent manuellement les mêmes champs scalaires puis les mêmes configurations dédupliquées et, si nécessaire, les niveaux d’indentation. Un ajout futur peut modifier l’un sans l’autre.

**Condition :** Évolution de LlReading, nouveaux états influençant la continuation ou coûts.

**Coût ou dette :** Coût de maintenance : deux listes de dépendances qui doivent rester exactement cohérentes ; aucun bug actuel de divergence affirmé.

**Usages et visibilité :** Recherche textuelle dans src, tests, bench, examples, templates, tools et cmake. ll_search_future: 2 fichiers, dont src/engines/cursiv/ll/runtime/ll_search.hpp, tests/engines/ll/LlSearchBound_test.cpp ; ll_search_same_future: 2 fichiers, dont src/engines/cursiv/ll/runtime/LlSearch.hpp, src/engines/cursiv/ll/runtime/ll_search.hpp. Les instanciations et appels émis comme chaînes restent inclus dans cette lecture ; ce décompte ne prouve pas une utilisation externe.

**Changement proposé :** Extraire une petite projection des scalaires ou un visiteur de champs consommé par hash et equality ; laisser explicites le parcours des configurations et l’égalité par contenu des indentations.

**Invariants à préserver :** Le hash est un filtre ; l’égalité exacte doit subsister. Exclure les champs de préférence volontairement hors futur comme owing sans changer la dominance.

**Validation future — non exécutée :** Revue champ par champ et assertions ciblées après modification sur différences d’un seul champ ; vérifier coût et code généré.

**Preuves dans la révision auditée :**

- [src/engines/cursiv/ll/runtime/ll_search.hpp:78](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/ll/runtime/ll_search.hpp#L78) — hash
- [src/engines/cursiv/ll/runtime/ll_search.hpp:119](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/ll/runtime/ll_search.hpp#L119) — égalité

<a id="finding-m-ll-caret-replay-windows"></a>
### M-LL-CARET-REPLAY-WINDOWS — Attribuer puis réduire les replays de préfixes recouvrants au caret

**optimisation · P2 · candidat · confiance moyenne.** Périmètre principal : **LL-045 — Replay progressif et filtrage du préfixe**. Périmètres associés : LL-045.

**Frontière :** Contexte candidat et fenêtre du CST → configurations simulées compatibles avec le préfixe.

**Observation :** La boucle widths réinitialise sim puis ll_replay reconstruit la liste de feuilles et rejoue la fenêtre. La largeur named progresse géométriquement, mais record(false) accepte dès plus de last+1 feuilles ; des ancêtres successifs peuvent donc faire relire beaucoup de préfixes recouvrants.

**Condition :** Caret dont les retours restent inconnus au travers de nombreuses frontières de records, nécessitant plusieurs fenêtres avant décision.

**Coût ou dette :** Somme des largeurs rejouées, potentiellement superlinéaire si les fenêtres s’accroissent par petits incréments ; aucun cas pathologique exécuté ici.

**Usages et visibilité :** Recherche textuelle dans src, tests, bench, examples, templates, tools et cmake. ll_replay: 1 fichiers, dont src/engines/cursiv/ll/runtime/ll_caret.hpp ; LlReplayWidth: 1 fichiers, dont src/engines/cursiv/ll/runtime/ll_caret.hpp. Les instanciations et appels émis comme chaînes restent inclus dans cette lecture ; ce décompte ne prouve pas une utilisation externe.

**Changement proposé :** Instrumenter nombre et largeur des tentatives, puis choisir une croissance géométrique minimale ou des checkpoints compatibles ; la collecte backward des feuilles peut être partagée même si les simulations ne le sont pas.

**Invariants à préserver :** Conserver frontières fiables, signaux shallow/named/broken, conditions lues avant le caret, gardes qui regardent après resume et reconstruction des callers.

**Validation future — non exécutée :** Comparer toutes NextFeature/membres sur CST réparés et sources profondes ; mesurer mémoire des checkpoints et assurer absence de rétention par document.

**Preuves dans la révision auditée :**

- [src/engines/cursiv/ll/runtime/ll_caret.hpp:283](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/ll/runtime/ll_caret.hpp#L283) — croissance selon frontières
- [src/engines/cursiv/ll/runtime/ll_caret.hpp:537](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/ll/runtime/ll_caret.hpp#L537) — collecte par tentative
- [src/engines/cursiv/ll/runtime/ll_caret.hpp:951](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/ll/runtime/ll_caret.hpp#L951) — forget et replay

<a id="finding-m-lex-alias-active"></a>
### M-LEX-ALIAS-ACTIVE — Recherche linéaire de chaque alias dans la pile active

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **LEX-002 — Réification des terminaux et résolution des alias**. Périmètres associés : LEX-002.

**Frontière :** Expressions lexicales du DSL → nœuds canoniques et terminaux nommés.

**Observation :** resolve_rule parcourt path avec ranges::find à chaque descente, puis empile/dépile la règle. Une chaîne de d alias distincts peut cumuler 1+…+d comparaisons avant toute récursion cyclique. resolve copie aussi la pile initiale, mais le constat principal porte sur l’appartenance répétée.

**Condition :** Terminaux atteignant de longues chaînes d’alias ou sous-expressions réutilisées sous de nombreux alias.

**Coût ou dette :** O(d²) comparaisons d’identifiants pour une descente linéaire de profondeur d ; pas de mesure de temps.

**Usages et visibilité :** Recherche textuelle dans src, tests, bench, examples, templates, tools et cmake. LexReifier: 3 fichiers, dont src/engines/cursiv/shared/gen/Lowerer.hpp, src/engines/cursiv/shared/gen/LexReifier.hpp, src/engines/cursiv/shared/gen/LexReifier.cpp ; resolve_rule: 2 fichiers, dont src/engines/cursiv/shared/gen/LexReifier.hpp, src/engines/cursiv/shared/gen/LexReifier.cpp ; terminal_body: 3 fichiers, dont src/engines/cursiv/shared/gen/LexReifier.hpp, src/engines/cursiv/shared/gen/LexReifier.cpp, src/engines/cursiv/shared/gen/Lowerer.cpp. Les instanciations et appels émis comme chaînes restent inclus dans cette lecture ; ce décompte ne prouve pas une utilisation externe.

**Changement proposé :** Conserver la pile ordonnée et ajouter un marquage actif par identifiant de règle, initialisé depuis path, partagé uniquement pendant cette résolution. Évaluer la mémoire selon le nombre de règles avant de choisir tableau ou ensemble.

**Invariants à préserver :** Le marquage doit être retiré au retour ; ne pas transformer une visite terminée en cycle. cfg, terminal_body, présence des captures et fallback call restent inchangés.

**Validation future — non exécutée :** Après correction : comparer image lexicale et calls émis pour chaînes, diamant, cycles et captures dans plusieurs sections ; contrôler le nombre de recherches sans reproducer dans cette campagne.

**Preuves dans la révision auditée :**

- [src/engines/cursiv/shared/gen/LexReifier.cpp:138](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/shared/gen/LexReifier.cpp#L138) — Copie de pile initiale
- [src/engines/cursiv/shared/gen/LexReifier.cpp:153](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/shared/gen/LexReifier.cpp#L153) — Recherche de chaque règle active
- [src/engines/cursiv/shared/gen/LexReifier.cpp:158](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/shared/gen/LexReifier.cpp#L158) — Les captures empêchent l’expansion

<a id="finding-m-lex-guard-intern"></a>
### M-LEX-GUARD-INTERN — Internage linéaire des guards NFA

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **LEX-005 — Construction du NFA lexical**. Périmètres associés : LEX-005.

**Frontière :** Nœuds lexicaux → états NFA, branches, répétitions, assertions et relais.

**Observation :** LexNfaPool::guard recherche node et out dans tous les guards existants avant d’ajouter un hole. Pour G clés nouvelles, le nombre de comparaisons croît comme G(G−1)/2. Le pool possède déjà un index de sinks distinct, sans index équivalent pour les guards.

**Condition :** Grammaires avec beaucoup de refus/guards distincts ou appels répétés vers des guards déjà éloignés dans le tableau.

**Coût ou dette :** O(G²) comparaisons à la création de G guards ; l’ordre de stockage et les trous restent utiles.

**Usages et visibilité :** Recherche textuelle dans src, tests, bench, examples, templates, tools et cmake. LexNfaPool: 14 fichiers, dont src/engines/cursiv/shared/gen/HeadMatcher.hpp, src/engines/cursiv/shared/gen/LexOvertakes.hpp, src/engines/cursiv/shared/gen/LexDfaBuild.hpp, src/engines/cursiv/shared/gen/LexAlphabet.hpp, src/engines/cursiv/shared/gen/LexRefusalProduct.hpp ; LexNfaBuild: 3 fichiers, dont src/engines/cursiv/shared/gen/LexAlphabet.hpp, src/engines/cursiv/shared/gen/LexRefusalProduct.hpp, src/engines/cursiv/shared/gen/LexNfaBuild.hpp ; lex_push_relay: 2 fichiers, dont src/engines/cursiv/shared/gen/LexRefusalProduct.hpp, src/engines/cursiv/shared/gen/LexNfaBuild.hpp. Les instanciations et appels émis comme chaînes restent inclus dans cette lecture ; ce décompte ne prouve pas une utilisation externe.

**Changement proposé :** Ajouter un index (node,out) → hole, en gardant le vecteur guards et son ordre de première création. Ne pas remplacer la clé par le seul node.

**Invariants à préserver :** La première création retient term pour le diagnostic ; l’index doit conserver ce comportement et ne pas fusionner des continuations out différentes. La croissance de n.s ne change pas les indices.

**Validation future — non exécutée :** Après correction : comparer graphes et diagnostics de guards partagés entre terminaux ; contrôler que même clé réutilise le même hole et que continuations différentes restent séparées.

**Preuves dans la révision auditée :**

- [src/engines/cursiv/shared/gen/LexNfaPool.hpp:115](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/shared/gen/LexNfaPool.hpp#L115) — Recherche node/out et conservation de la première provenance term
- [src/engines/cursiv/shared/gen/LexNfaPool.hpp:195](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/shared/gen/LexNfaPool.hpp#L195) — Index existant pour les sinks, contrat distinct

<a id="finding-m-lex-partition-queued"></a>
### M-LEX-PARTITION-QUEUED — Tableau local queued écrit mais jamais lu

**mort · P3 · établi · confiance forte.** Périmètre principal : **LEX-007 — Minimisation et remplissage des tables lexicales**. Périmètres associés : LEX-007.

**Frontière :** DFA construit → classes d’états fusionnées et tables exportées avec acceptations et relais.

**Observation :** Dans partition_refine, queued est construit à 1, mis à 0 lors du dépilement et étendu à 1 à la création d’une partition. Aucun test, retour, capture ni appel ne lit ce vecteur dans la fonction. Le scheduling dépend uniquement de work ; queued n’est ni une API ni une donnée exportée.

**Condition :** Chaque minimisation utilisant partition_refine.

**Coût ou dette :** Un vecteur de caractères proportionnel au nombre de blocs, ses allocations et trois sites de code sans effet algorithmique observable.

**Usages et visibilité :** Recherche textuelle dans src, tests, bench, examples, templates, tools et cmake. partition_refine: 3 fichiers, dont src/engines/cursiv/shared/gen/LexDfaBuild.hpp, src/engines/cursiv/shared/gen/PartitionRefine.hpp, src/engines/cursiv/lr/gen/lr_build_passes.cpp ; LexRelayGen: 13 fichiers, dont src/engines/cursiv/shared/gen/LexDfaBuild.hpp, src/engines/cursiv/shared/gen/LexRefusalProduct.hpp, src/engines/cursiv/shared/gen/LexNfaBuild.hpp, src/engines/cursiv/shared/gen/LexicalPredicateEmitter.hpp, src/engines/cursiv/shared/gen/LexNfaPool.hpp. Les instanciations et appels émis comme chaînes restent inclus dans cette lecture ; ce décompte ne prouve pas une utilisation externe.

**Changement proposé :** Supprimer uniquement la déclaration de queued et les deux écritures queued[a]=0 / queued.push_back(1).

**Invariants à préserver :** Ne pas introduire un filtrage de work à partir de queued ; préserver exactement l’ordre de travail, les partitions, les compteurs moves/reads et la stratégie smaller-side.

**Validation future — non exécutée :** Après correction : revue du diff limité aux trois sites et vérifications existantes de partition/minimisation ; aucun test/reproducer exécuté pendant cet audit.

**Preuves dans la révision auditée :**

- [src/engines/cursiv/shared/gen/PartitionRefine.hpp:56](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/shared/gen/PartitionRefine.hpp#L56) — Déclaration locale
- [src/engines/cursiv/shared/gen/PartitionRefine.hpp:61](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/shared/gen/PartitionRefine.hpp#L61) — Écriture après pop
- [src/engines/cursiv/shared/gen/PartitionRefine.hpp:98](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/shared/gen/PartitionRefine.hpp#L98) — Écriture lors du split

<a id="finding-m-lex-words-multicursor"></a>
### M-LEX-WORDS-MULTICURSOR — L’index du premier octet est abandonné avec plusieurs curseurs

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **LEX-017 — Machine lexicale, environnements de captures et retour**. Périmètres associés : LEX-017.

**Frontière :** Expression lexicale non régulière et état initial → états de fin, captures et reprises de callers.

**Observation :** lexical_words dispose de w.opens/w.order et les consulte lorsque in.size()==1. Sinon il parcourt chaque mot puis chaque curseur et appelle lexical_word, y compris les combinaisons dont le premier octet suffit à exclure le mot.

**Condition :** Reconnaissance lexicale à plusieurs endpoints/captures devant une table de nombreux mots.

**Coût ou dette :** W×C essais avant prise en compte des longueurs, W mots et C curseurs ; l’index existe déjà et peut réduire les candidats sans garantie de gain mesuré.

**Usages et visibilité :** Recherche textuelle dans src, tests, bench, examples, templates, tools et cmake. LexicalMachine: 6 fichiers, dont src/engines/cursiv/shared/gen/LexicalPredicateEmitter.hpp, src/engines/cursiv/shared/runtime/LexicalMachine.hpp, src/engines/cursiv/lr/gen/lr_emit_parser.cpp, src/engines/cursiv/ll/gen/ll_emit_table.cpp, tests/engines/shared/PredicateState_test.cpp ; LexicalCaptures: 3 fichiers, dont src/engines/cursiv/shared/gen/LexicalPredicateEmitter.hpp, src/engines/cursiv/shared/runtime/LexicalMachine.hpp, tests/engines/shared/LexicalBackends_test.cpp ; lexical_words: 3 fichiers, dont src/engines/cursiv/shared/gen/LexicalPredicateEmitter.hpp, src/engines/cursiv/shared/runtime/LexicalMachine.hpp, tests/engines/ll/LlGeneration_test.cpp ; PredicateStates: 4 fichiers, dont src/engines/cursiv/shared/table/LexRelay.hpp, src/engines/cursiv/shared/runtime/LexicalMachine.hpp, src/engines/cursiv/shared/runtime/PredicateState.hpp, tests/engines/ll/LlPredicates_test.cpp. Les instanciations et appels émis comme chaînes restent inclus dans cette lecture ; ce décompte ne prouve pas une utilisation externe.

**Changement proposé :** Employer les listes opens/order pour constituer les combinaisons candidates ; commencer par Final=false. Pour Final=true, conserver une élection selon l’ordre global des mots, puis réunir les curseurs qui lisent le premier mot accepté.

**Invariants à préserver :** Final=true ne signifie pas premier mot par curseur : tous les résultats doivent provenir du même premier mot global. Conserver captures, déduplication des sorties et horizon seen ; les exclusions de premier octet ne doivent pas effacer un octet/scalar effectivement inspecté.

**Validation future — non exécutée :** Après correction : comparer endpoints/captures et seen pour plusieurs curseurs, mots partageant un préfixe, EOF et classes Unicode ; vérifier séparément Final=false et Final=true.

**Preuves dans la révision auditée :**

- [src/engines/cursiv/shared/runtime/LexicalMachine.hpp:141](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/shared/runtime/LexicalMachine.hpp#L141) — Index sur singleton
- [src/engines/cursiv/shared/runtime/LexicalMachine.hpp:154](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/shared/runtime/LexicalMachine.hpp#L154) — Produit mots × curseurs
- [src/engines/cursiv/shared/runtime/LexicalMachine.hpp:159](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/shared/runtime/LexicalMachine.hpp#L159) — Arrêt sur le premier mot global ayant des sorties

<a id="finding-m-lex-gap-bounds"></a>
### M-LEX-GAP-BOUNDS — Recalcul des bornes d’une même expression de gap

**optimisation · P3 · candidat · confiance moyenne.** Périmètre principal : **LEX-021 — Classification et émission des expressions de gap**. Périmètres associés : LEX-021.

**Frontière :** Expressions entre tokens → classification gap/phrase, coupures et programme généré.

**Observation :** GapReach::of calcule bounds(leaf.node), puis appelle skips(v), qui recalcule bounds du même node lorsque skip est absent. widest répète of pour chaque condition, et skips_any matérialise lui aussi la liste leaves. Les bornes dépendent de la configuration lue, pas du texte.

**Condition :** Conditions nombreuses partageant des expressions larges, ou demandes répétées de of/widest/skips_any sur une configuration immuable.

**Coût ou dette :** Parcours récursifs et vecteurs temporaires répétés ; ampleur dépendant des partages et du nombre de requêtes, gain non établi.

**Usages et visibilité :** Recherche textuelle dans src, tests, bench, examples, templates, tools et cmake. gap_leaf: 3 fichiers, dont src/engines/cursiv/shared/gen/Lowerer.hpp, src/engines/cursiv/shared/gen/LowererGap.cpp, src/engines/cursiv/lr/gen/lr_emit_parser.cpp ; phrase_gap: 2 fichiers, dont src/engines/cursiv/shared/gen/Lowerer.hpp, src/engines/cursiv/shared/gen/LowererGap.cpp ; GapReach: 5 fichiers, dont src/engines/cursiv/shared/gen/PredicateModes.hpp, src/engines/cursiv/shared/gen/GapReach.hpp, src/engines/cursiv/shared/gen/UnboundedCondition.cpp, src/engines/cursiv/lr/gen/lr_emit_parser.cpp, src/engines/cursiv/ll/gen/ll_emit_table.cpp ; GapEmitter: 4 fichiers, dont src/engines/cursiv/shared/gen/PredicateEmitter.hpp, src/engines/cursiv/shared/gen/GapEmitter.hpp, src/engines/cursiv/lr/gen/lr_emit_parser.cpp, src/engines/cursiv/ll/gen/ll_emit_table.cpp. Les instanciations et appels émis comme chaînes restent inclus dans cette lecture ; ce décompte ne prouve pas une utilisation externe.

**Changement proposé :** Commencer par passer la borne déjà calculée au test skips dans of ; envisager ensuite un mémo local par node dans une instance de GapReach si la configuration est stable pendant sa vie.

**Invariants à préserver :** Ne pas mettre en cache au-delà d’une mutation de cfg ; préserver saturation kEndless, overhang scalaire, skips et count des tokens. Éviter une allocation persistante pour les petites grammaires sans données de coût.

**Validation future — non exécutée :** Après correction : vérifier les valeurs CondReach pour séquences, alternatives, répétitions, tokens avec trivia et expressions partagées ; mesurer ultérieurement allocations et visites si cette piste est priorisée.

**Preuves dans la révision auditée :**

- [src/engines/cursiv/shared/gen/GapReach.hpp:23](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/shared/gen/GapReach.hpp#L23) — skips recalcule bounds
- [src/engines/cursiv/shared/gen/GapReach.hpp:60](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/shared/gen/GapReach.hpp#L60) — of calcule déjà la même borne
- [src/engines/cursiv/shared/gen/GapReach.hpp:79](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/shared/gen/GapReach.hpp#L79) — widest parcourt chaque condition

<a id="finding-m-lex-gap-span-words"></a>
### M-LEX-GAP-SPAN-WORDS — Union et différence de spans traitées position par position

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **LEX-022 — Reconnaissance des gaps et ensembles de positions**. Périmètres associés : LEX-022.

**Frontière :** Portion intertoken et expression compilée → positions atteignables et verdict de gap complet.

**Observation :** Span::unite appelle each puis set pour chaque bit de l’autre ensemble ; without parcourt chaque bit du premier puis has/set. La structure stocke déjà des uint64_t et les opérations équivalentes de Word sont bitwise.

**Condition :** Gaps longs dont les expressions atteignent de nombreuses positions, particulièrement les ensembles denses.

**Coût ou dette :** Travail proportionnel aux bits présents et appels de croissance/positionnement par bit, au lieu d’une opération par mot recouvrant ; pas de gain chiffré.

**Usages et visibilité :** Recherche textuelle dans src, tests, bench, examples, templates, tools et cmake. GapPositions: 6 fichiers, dont src/engines/cursiv/shared/runtime/GapPositions.hpp, src/engines/cursiv/lr/gen/lr_emit_parser.cpp, src/engines/cursiv/ll/gen/ll_emit_table.cpp, tests/engines/shared/GapReference.hpp, tests/engines/lr/LrRuntime_test.cpp ; gap_repeat: 4 fichiers, dont src/engines/cursiv/shared/gen/LexicalPredicateEmitter.hpp, src/engines/cursiv/shared/gen/GapEmitter.hpp, src/engines/cursiv/shared/runtime/GapPositions.hpp, tests/engines/lr/LrRuntime_test.cpp ; gap_matches: 3 fichiers, dont src/engines/cursiv/shared/gen/GapEmitter.hpp, src/engines/cursiv/shared/runtime/GapPositions.hpp, tests/engines/lr/LrRuntime_test.cpp. Les instanciations et appels émis comme chaînes restent inclus dans cette lecture ; ce décompte ne prouve pas une utilisation externe.

**Changement proposé :** Aligner les bases des spans, réserver une fois la plage utile puis appliquer OR ou ANDNOT mot par mot ; conserver un chemin adapté aux ensembles très épars si nécessaire.

**Invariants à préserver :** empty repose sur w.empty et operator== compare base/w par défaut. Supprimer les mots nuls de bord pour retrouver la représentation produite par set, sans supprimer les zéros intérieurs ; traiter auto-union, spans disjoints et span vide. Conserver les limites TextOffset.

**Validation future — non exécutée :** Après correction : égalité des ensembles et de leur représentation pour vide, disjoint, recouvrant, base décalée et différence vide ; vérifier terminaisons de gap_repeat et verdicts whole ; contrôle statique seulement ici.

**Preuves dans la révision auditée :**

- [src/engines/cursiv/shared/runtime/GapPositions.hpp:37](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/shared/runtime/GapPositions.hpp#L37) — empty dépend de la représentation
- [src/engines/cursiv/shared/runtime/GapPositions.hpp:60](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/shared/runtime/GapPositions.hpp#L60) — Union bit par bit
- [src/engines/cursiv/shared/runtime/GapPositions.hpp:63](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/shared/runtime/GapPositions.hpp#L63) — Différence bit par bit
- [src/engines/cursiv/shared/runtime/GapPositions.hpp:70](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/shared/runtime/GapPositions.hpp#L70) — Égalité structurelle par défaut

<a id="finding-m-lr-lower-depth"></a>
### M-LR-LOWER-DEPTH — Les parcours de formes LR conservent une dépendance à la pile native

**simplification · P2 · établi · confiance forte.** Périmètre principal : **LR-003 — Application des directives aux formes intermédiaires**. Périmètres associés : LR-002, LR-003, LR-004, LR-009.

**Frontière :** Expression de grammaire → shapes portant création, écriture, flags, conditions et bords.

**Observation :** ended, ambient, copy, head_turns, holds_turn, lr_emit_alts et lr_clone_opening descendent encore récursivement dans les formes ou alias. La mémoïsation de ended et des clones évite certaines revisites mais ne borne pas la profondeur du premier parcours.

**Condition :** Grammaires à directives/groupes/alias profondément imbriqués ; les séquences ordinaires aplaties itérativement ne couvrent pas ces branches.

**Coût ou dette :** O(D) frames natives pour une profondeur D, et éventuellement copies de chaînes d’actions à chaque niveau. Aucun seuil de débordement ni temps mesuré.

**Usages et visibilité :** Appels suivis depuis lr_lower/LrLowerer::run, lowering des répétitions/groupes et spécialisation des opérateurs ; LrLowering est consommé par générateur et tests LR/shared.

**Changement proposé :** Convertir séparément ces marches en piles explicites, avec une continuation portant contexte et résultat ; commencer par copy/ambient puis les alias. Ne pas créer un visiteur générique masquant les différences.

**Invariants à préserver :** Ordre innermost-first des writes, bords lead/trail, spécialisation par configuration, identité et moment d’insertion des clones, arrêt des branches et cache des résultats.

**Validation future — non exécutée :** Après correction, comparer CFG/AST et diagnostics de grammaires profondes avec directives, scopes, EOF, unordered et opérateurs ; vérifier mémoire native/WASM.

**Preuves dans la révision auditée :**

- [src/engines/cursiv/lr/gen/LrLowerer.cpp:47](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/gen/LrLowerer.cpp#L47) — La transformation memoïsée appelle ended sur les enfants.
- [src/engines/cursiv/lr/gen/LrLowerLay.cpp:58](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/gen/LrLowerLay.cpp#L58) — Descente récursive des bords de régions.
- [src/engines/cursiv/lr/gen/LrLowerPool.hpp:191](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/gen/LrLowerPool.hpp#L191) — Copie récursive du sous-arbre et de ses listes.
- [src/engines/cursiv/lr/gen/lr_lower_epsilon.hpp:379](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/gen/lr_lower_epsilon.hpp#L379) — Distribution récursive des alternatives.
- [src/engines/cursiv/lr/gen/lr_lower_settle.hpp:668](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/gen/lr_lower_settle.hpp#L668) — Clonage récursif des règles en ouverture.

<a id="finding-m-lr-lower-gap-index"></a>
### M-LR-LOWER-GAP-INDEX — Le classement des règles de condition recherche à répétition dans les listes inverses

**optimisation · P3 · établi · confiance forte.** Périmètre principal : **LR-003 — Application des directives aux formes intermédiaires**. Périmètres associés : LR-003.

**Frontière :** Expression de grammaire → shapes portant création, écriture, flags, conditions et bords.

**Observation :** is_gap cherche le non-terminal dans gapNt puis stop_of cherche dans stopNt ; ces listes sont indexées dans le sens condition/cut vers non-terminal. Les appels de lowering font donc une recherche inverse linéaire répétée.

**Condition :** Nombre élevé de conditions/commit exits et nombreux slots, y compris head_turns, répétitions et calcul bare par production.

**Coût ou dette :** Q appels coûtent jusqu’à O(Q×(G+C)) comparaisons pour G conditions et C règles stop. Le domaine des IDs non-terminaux est déjà compact.

**Usages et visibilité :** Usages internes LrLowerLay, lr_lower_define et lr_lower_epsilon recherchés ; gapNt/stopNt restent nécessaires à la création et au retour du cut.

**Changement proposé :** Ajouter un petit index inverse par non-terminal donnant kind et cut+1, rempli lors de lay_condition/stop_rule ; conserver les listes directes si leurs lecteurs en ont besoin.

**Invariants à préserver :** Différence condition versus stop, sentinelle zéro, terminal jamais gap, identité d’un stop fermé sous une autre configuration.

**Validation future — non exécutée :** Après modification vérifier conditions seules, arrêts de commit, régions et règles ordinaires ; mesurer appels et taille de l’index.

**Preuves dans la révision auditée :**

- [src/engines/cursiv/lr/gen/LrLowerPool.hpp:98](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/gen/LrLowerPool.hpp#L98) — Deux recherches inverses possibles.
- [src/engines/cursiv/lr/gen/LrLowerPool.hpp:106](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/gen/LrLowerPool.hpp#L106) — Parcours de stopNt.
- [src/engines/cursiv/lr/gen/lr_lower_epsilon.hpp:169](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/gen/lr_lower_epsilon.hpp#L169) — is_gap exécuté sur les positions de chaque subset.

<a id="finding-m-lr-aux-fixpoint-keys"></a>
### M-LR-AUX-FIXPOINT-KEYS — La fusion des auxiliaires reconstruit toutes les clés à chaque tour

**optimisation · P2 · candidat · confiance moyenne.** Périmètre principal : **LR-006 — Identité des productions et compaction du CFG**. Périmètres associés : LR-006.

**Frontière :** Productions/auxiliaires provisoires → CFG compact avec références remappées.

**Observation :** lr_intern_aux reconstruit pour chaque auxiliaire survivant un vector de métadonnées, décisions et slots, l’insère dans une map ordonnée, puis rescane tous les RHS lorsque des fusions ont eu lieu. Une fusion locale peut obliger à plusieurs tours.

**Condition :** Chaînes d’auxiliaires dont les parents ne deviennent égaux qu’après fusion des enfants.

**Coût ou dette :** Pour I tours, les clés et le remapping retraversent le volume du CFG à chaque tour ; allocations des clés et comparaisons lexicographiques supplémentaires. Pas de gain mesuré.

**Usages et visibilité :** Appel de LrLowerer::run avant drop_twins ; aux conservés à leurs IDs, productions seules compactées.

**Changement proposé :** Étudier une file de parents affectés avec signatures recalculées localement, puis remapping final. Conserver une première version simple avant raffinement incrémental.

**Invariants à préserver :** Égalité exacte incluant décisions, priorités et configs, traitement du self par sentinelle, choix du représentant et startProd ; un hash ne vaut pas égalité.

**Validation future — non exécutée :** Comparer CFG et comportements sur auxiliaires récursifs identiques/différents, collisions provoquées, longues chaînes de fusions et écritures différentes.

**Preuves dans la révision auditée :**

- [src/engines/cursiv/lr/gen/lr_lower_settle.hpp:56](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/gen/lr_lower_settle.hpp#L56) — Construction de vector de clé complète.
- [src/engines/cursiv/lr/gen/lr_lower_settle.hpp:77](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/gen/lr_lower_settle.hpp#L77) — Fixpoint global.
- [src/engines/cursiv/lr/gen/lr_lower_settle.hpp:92](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/gen/lr_lower_settle.hpp#L92) — Remplacement sur tous RHS.

<a id="finding-m-lr-graph-growth"></a>
### M-LR-GRAPH-GROWTH — Les pools du graphe réservent leur capacité par bonds de seize

**optimisation · P2 · candidat · confiance moyenne.** Périmètre principal : **LR-011 — Kernels et graphe d'états LR**. Périmètres associés : LR-011.

**Frontière :** CFG, domaines d'items et contexte → kernels internés, fermetures et arêtes de transition.

**Observation :** lr_room multiplie la capacité par16 jusqu’à want puis reserve ; lr_append applique cette politique aux kernels, closures et arêtes. Plusieurs pools indépendants peuvent ainsi retenir beaucoup de place libre immédiatement après croissance.

**Condition :** Grammaires faisant dépasser légèrement une capacité, surtout avec kernels/lookaheads larges et coexistence du core pendant un build wide.

**Coût ou dette :** La nouvelle capacité peut approcher16 fois le besoin franchissant l’ancien seuil ; ancien et nouveau stockage peuvent coexister durant reserve. Ce facteur concerne les capacités, pas un gain de mémoire totale mesuré.

**Usages et visibilité :** lr_append dans LrEdges et lr_graph ; poolHint initial dom.n×4 puis politique identique aux différents types de cellules.

**Changement proposé :** Comparer une croissance géométrique plus modérée ou un reserve adapté au prochain append, et un seuil en octets par type ; retenir seulement après mesures des allocations et du pic.

**Invariants à préserver :** Stabilité des IDs et réacquisition des pointeurs après croissance, ordre des états ; ne pas conserver un pointeur jusque-là invalidé.

**Validation future — non exécutée :** Mesurer capacité/taille et pic aux seuils de croissance sur core/wide, grammaires larges et cible mémoire contrainte.

**Preuves dans la révision auditée :**

- [src/engines/cursiv/lr/gen/lr_build.cpp:157](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/gen/lr_build.cpp#L157) — Politique de croissance explicite.
- [src/engines/cursiv/lr/gen/lr_build.cpp:163](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/gen/lr_build.cpp#L163) — Même politique pour chaque append.
- [src/engines/cursiv/lr/gen/lr_build.cpp:521](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/gen/lr_build.cpp#L521) — Réserve initiale des pools.

<a id="finding-m-lr-guard-fixpoint"></a>
### M-LR-GUARD-FIXPOINT — Le point fixe des exigences rescane toute la fermeture d’un état

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **LR-012 — Conditions et programmes de goto gardés**. Périmètres associés : LR-012.

**Frontière :** Conditions des slots et chemins de transition → exigences logiques et programmes de cibles.

**Observation :** Dans lr_graph, gamma est propagé par une boucle changed qui reparcourt tous les items csn, reconstruit des unions own/gamma puis applique lr_req_add. Les items non affectés sont revisités à chaque tour. Les autres propagations de ce générateur disposent déjà de files de lecteurs.

**Condition :** États gardés et chaînes de dépendances dont les exigences ne se propagent qu’en plusieurs tours.

**Coût ou dette :** I tours ×C items inspectés, auxquels s’ajoutent unions de bitsets et scans de l’antichaîne LrReq ; coût non borné par les seules exigences finalement conservées.

**Usages et visibilité :** Bloc gated de lr_graph<core/wide>, alimenté par lr_guard_positions et consommé par valuations des transitions.

**Changement proposé :** Construire les lecteurs de gamma par lhs dans la fermeture et mettre en file seulement ceux dont les exigences ont logiquement grandi ; dédupliquer les notifications.

**Invariants à préserver :** Réduction de l’antichaîne par inclusion, effet des propres guards des kernels, auto-appels qui modifient la liste lue, identité des tags et ordre des valuations.

**Validation future — non exécutée :** Comparer toutes les cibles/conds/alts et refus sur guards imbriqués, récursifs, chemins alternatifs et conjonctions redondantes ; compter visites.

**Preuves dans la révision auditée :**

- [src/engines/cursiv/lr/gen/lr_build.cpp:794](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/gen/lr_build.cpp#L794) — Point fixe global du bloc gamma.
- [src/engines/cursiv/lr/gen/lr_build.cpp:811](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/gen/lr_build.cpp#L811) — Reconstruction des exigences à chaque tour.
- [src/engines/cursiv/lr/gen/lr_build.cpp:66](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/gen/lr_build.cpp#L66) — Antichaîne conservant les exigences minimales.

<a id="finding-m-lr-group-flag-sets"></a>
### M-LR-GROUP-FLAG-SETS — Les lignes lexicales distinctes d’un groupe sont reconnues par recherche linéaire

**optimisation · P3 · établi · confiance forte.** Périmètre principal : **LR-014 — Refusion du graphe large**. Périmètres associés : LR-014.

**Frontière :** États avec lookaheads distincts → classes compatibles et références de groupe.

**Observation :** lr_group_flags reconstruit chaque ligne d’actions en DynSet et recherche son égal dans parts[group] avec ranges::find ; la liste peut contenir de nombreuses lectures canoniques distinctes.

**Condition :** Graphe wide refusionnant un grand nombre de lectures d’un même core.

**Coût ou dette :** K lignes distinctes dans un groupe demandent O(K²) égalités de sets, chacune proportionnelle aux mots comparés.

**Usages et visibilité :** lr_group_flags exécuté pour WithLa après refusion ; les parties servent ensuite à LexOvertakes, donc ne doivent pas être supprimées comme doublon du merged.

**Changement proposé :** Interner les sets par hash+égalité exacte ou réutiliser un index local de sets ; garder un vector d’IDs dans l’ordre de première apparition.

**Invariants à préserver :** Union merged et chaque partie distincte, ordre déterministe des demandes d’overtakes, aucune assimilation d’un hash à une identité.

**Validation future — non exécutée :** Comparer flags/modes et diagnostics sur nombreux lookaheads distincts puis répétés ; mesurer comparaisons de sets.

**Preuves dans la révision auditée :**

- [src/engines/cursiv/lr/gen/lr_build.cpp:1581](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/gen/lr_build.cpp#L1581) — Liste distincte par groupe.
- [src/engines/cursiv/lr/gen/lr_build.cpp:1586](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/gen/lr_build.cpp#L1586) — Reconnaissance linéaire des lignes.

<a id="finding-m-lr-order-score-dense"></a>
### M-LR-ORDER-SCORE-DENSE — Chaque couple production-token interrogé alloue une ligne sur tout le domaine d’items

**optimisation · P2 · candidat · confiance moyenne.** Périmètre principal : **LR-015 — Contextes et scores de l'ordre écrit**. Périmètres associés : LR-015.

**Frontière :** Items et chemins de retour → contextes admissibles et scores mémoïsés de comparaisons.

**Observation :** scores(p,t) initialise un vector<int8_t> de _dom.n cases à -1 ; score ne remplit ensuite que les items interrogés. parent_items(p), calculé paresseusement, rescane en outre l’ensemble des productions/slots pour découvrir les parents. trim ne libère pas ces lignes de scores.

**Condition :** Beaucoup de couples (production,token) comparés mais peu de parents admissibles par couple.

**Coût ou dette :** U couples allouent O(U×I) octets de scores pour I items, plus initialisations ; Q productions interrogées retraversent O(Q×L) slots pour parent_items. Gain conditionnel à la densité effective.

**Usages et visibilité :** LrWrittenOrder::generic, climb_class et rise_of utilisent scores ; tableau vivant durant le merge des conflits, uniquement après premier ordre demandé.

**Changement proposé :** Étudier des scores indexés par parents possibles et un index inverse item/production construit une fois ; voie dense conservée pour petits domaines. Comptabiliser ce cache séparément du seuil des layouts.

**Invariants à préserver :** Score0/1/2, choix du maximum, indépendance du contexte t et production, durée des références à une ligne pendant insertion d’autres lignes.

**Validation future — non exécutée :** Comparer verdict/exact sur conflits epsilon, nullable et ascendances ; mesurer couples, items effectivement notés, mémoire et temps de génération.

**Preuves dans la révision auditée :**

- [src/engines/cursiv/lr/gen/lr_build.cpp:2277](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/gen/lr_build.cpp#L2277) — Initialisation dense par couple.
- [src/engines/cursiv/lr/gen/lr_build.cpp:2742](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/gen/lr_build.cpp#L2742) — Recherche des parents sur les productions.
- [src/engines/cursiv/lr/gen/lr_build.cpp:3663](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/gen/lr_build.cpp#L3663) — Cache conservé pendant WrittenOrder.

<a id="finding-m-lr-unit-early-snapshot"></a>
### M-LR-UNIT-EARLY-SNAPSHOT — L’élimination des unités copie l’automate avant de savoir si une cellule est éligible

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **LR-021 — Élimination des productions unitaires**. Périmètres associés : LR-021.

**Frontière :** Automate et unités admissibles → états composés et transitions redirigées.

**Observation :** lr_unit_elimination copie actions, bases, comptes et gotos avant de construire hasE ; les copies ont donc lieu même lorsque aucune cellule isolée ne contient une réduction éligible et que le bloc de composition est entièrement sauté.

**Condition :** Toute génération passant par lr_unit_elimination, particulièrement les automates sans unité effaçable.

**Coût ou dette :** Copies O(A+S+G) et coexistence des pools originaux/snapshots pour A actions, S états, G gotos ; travail inutile sur la branche sans hasE.

**Usages et visibilité :** lr_build_impl appelle la passe après construction lexicale ; snapshots nécessaires pendant les mutations du bloc avec hasE, pas avant son admission.

**Changement proposé :** Calculer hasE en lecture sur les tableaux originaux, retourner s’il est vide, puis seulement matérialiser les snapshots utilisés par compose/targetOf.

**Invariants à préserver :** Le snapshot doit toujours précéder la première mutation ; conserver identité de terminal, ordre de mint et copie par valeur dans lr_carry.

**Validation future — non exécutée :** Vérifier sorties identiques avec zéro/une/plusieurs unités, conflits et guards ; compter allocations/copies du cas sans éligible après correction.

**Preuves dans la révision auditée :**

- [src/engines/cursiv/lr/gen/lr_build_passes.cpp:692](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/gen/lr_build_passes.cpp#L692) — Copie préalable de toutes les actions.
- [src/engines/cursiv/lr/gen/lr_build_passes.cpp:698](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/gen/lr_build_passes.cpp#L698) — Copie des gotos avant admission.
- [src/engines/cursiv/lr/gen/lr_build_passes.cpp:709](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/gen/lr_build_passes.cpp#L709) — Composition conditionnelle après copies.

<a id="finding-m-lr-multi-unused-union"></a>
### M-LR-MULTI-UNUSED-UNION — modeAct calcule une union qui n’est jamais relue

**mort · P3 · établi · confiance forte.** Périmètre principal : **LR-024 — Automates lexicaux et multiplicité des candidats**. Périmètres associés : LR-024.

**Frontière :** Modes/alphabet → DFA, candidats rivaux et flags de lecture conditionnelle.

**Observation :** Dans lr_multi_pass, le local modeAct est déclaré puis alimenté par modeAct |= reads(f) ; aucune lecture de sa valeur ne suit. La détection utilise modeTakes, et reads(f) reste séparément requis pour choisir le témoin de diagnostic. La recherche du symbole sur tout le fichier ne retourne que ces deux occurrences.

**Condition :** Chaque mode ayant des états parser, dans la passe réelle et éventuellement dans la passe sans acceptation EOF.

**Coût ou dette :** Un parcours des actions pour reads(f), construction/copie de DynSet et unions par état, sans contribution au résultat ; aucune économie chiffrée.

**Usages et visibilité :** Variable locale sans adresse/capture externe ; reads construit seulement un set depuis modes.trivia/joins et action_row. modeTakes et l’appel diagnostic restent actifs.

**Changement proposé :** Supprimer uniquement modeAct et son affectation dans la boucle ; ajuster le commentaire pour décrire l’union modeTakes effectivement utilisée.

**Invariants à préserver :** Conserver modeTakes, les joins/relayTerms, les appels reads du diagnostic, ordre du premier témoin et marqueurs modeMulti.

**Validation future — non exécutée :** Après correction comparer multiCand/modeWin/maxCands et diagnostics sur modes multiples et EOF ; compilation avertissements et compte allocations.

**Preuves dans la révision auditée :**

- [src/engines/cursiv/lr/gen/lr_build_lex.cpp:596](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/gen/lr_build_lex.cpp#L596) — Local déclaré sans consommateur.
- [src/engines/cursiv/lr/gen/lr_build_lex.cpp:598](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/gen/lr_build_lex.cpp#L598) — Unique affectation du local.
- [src/engines/cursiv/lr/gen/lr_build_lex.cpp:651](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/gen/lr_build_lex.cpp#L651) — Consommateur diagnostic de reads à garder.

<a id="finding-m-lr-multi-action-spans"></a>
### M-LR-MULTI-ACTION-SPANS — Le test d’indifférence balaie et copie les lignes au lieu de chercher leurs deux runs

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **LR-024 — Automates lexicaux et multiplicité des candidats**. Périmètres associés : LR-024.

**Frontière :** Modes/alphabet → DFA, candidats rivaux et flags de lecture conditionnelle.

**Observation :** Pour chaque paire (t,win) non cachée et chaque état du mode, indifferent balaie toute action_row, construit mine/theirs en vectors de(kind,arg), puis compare. Les lignes sont déjà triées par terminal et lr_term_run existe dans le même module.

**Condition :** Modes contenant plusieurs états et nombreux couples de candidats distincts ; le cache évite les répétitions de même paire mais pas le coût de sa première preuve.

**Coût ou dette :** Pour Q paires et A actions totales des états du mode, O(Q×A) inspections avec temporaires, même si les deux runs sont courts.

**Usages et visibilité :** indifferent appelé depuis lr_multi_pass ; lr_term_run utilisé ailleurs dans lr_build_lex et build_passes ; alts/exact volontairement exclus de cette preuve actuelle.

**Changement proposé :** Chercher les deux runs par lr_term_run/equal_range, comparer directement kind/arg et vérifier reduce sans les matérialiser.

**Invariants à préserver :** Ordre des actions, état sans réponse, answered au moins une fois, rejet dès différence ou action non reduce ; conserver le cache par paire.

**Validation future — non exécutée :** Comparer détection multi et témoins avec runs absents/identiques/multiples, guards et réductions ; compter éléments inspectés et allocations.

**Preuves dans la révision auditée :**

- [src/engines/cursiv/lr/gen/lr_build_lex.cpp:605](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/gen/lr_build_lex.cpp#L605) — Cache par paire.
- [src/engines/cursiv/lr/gen/lr_build_lex.cpp:610](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/gen/lr_build_lex.cpp#L610) — Deux temporaires par état.
- [src/engines/cursiv/lr/gen/lr_term_run.hpp:17](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/gen/lr_term_run.hpp#L17) — Recherche binaire déjà disponible.

<a id="finding-m-lr-sites-projection-cache"></a>
### M-LR-SITES-PROJECTION-CACHE — La projection d’un site-set est reconstruite pour chaque action qui le lit

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **LR-027 — Projection des sites vers les attentes et records**. Périmètres associés : LR-027.

**Frontière :** Chaînes d'écriture, sites de grammaire et actions LR → metadata de record et attentes de complétion.

**Observation :** sites_read crée un vector, canonicalise chaque site puis déduplique par find linéaire ; expect le rappelle pour chaque action et valuation de garde, alors que a.siteSets et sites sont immuables pendant toute dérivation.

**Condition :** Plusieurs états/actions réutilisent le même alts, ou un site-set contient de nombreux sites canoniques distincts.

**Coût ou dette :** Pour K lectures d’un set de M entrées, jusqu’à O(K×M²) comparaisons locales et K allocations/réassemblages.

**Usages et visibilité :** Appels directs dans expect pour act.alts et guard.alts ; résultats immédiatement parcourus, identité alts déjà internée dans BuildTable.

**Changement proposé :** Mémoriser paresseusement les projections par alts pendant expect ; dédupliquer avec index local si les sets larges le justifient.

**Invariants à préserver :** Première apparition des sites, AND de entered entre lectures égales, entrée kNoElement seulement si aucun site, durée de vie des spans.

**Validation future — non exécutée :** Comparer pool/items d’attentes et propositions avec guards, sites aliasés et mélange entered ; mesurer nombre de projections distinctes et mémoire.

**Preuves dans la révision auditée :**

- [src/engines/cursiv/lr/gen/lr_build_derived.cpp:152](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/gen/lr_build_derived.cpp#L152) — Résultat reconstruit à chaque appel.
- [src/engines/cursiv/lr/gen/lr_build_derived.cpp:157](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/gen/lr_build_derived.cpp#L157) — Déduplication linéaire.
- [src/engines/cursiv/lr/gen/lr_build_derived.cpp:207](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/gen/lr_build_derived.cpp#L207) — Réutilisations possibles par valuations.

<a id="finding-m-lr-elect-twin-pool"></a>
### M-LR-ELECT-TWIN-POOL — Une classe de productions jumelles est recopiée pour chacun de ses membres

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **LR-028 — Encodage des mots d'élection et renommages**. Périmètres associés : LR-028.

**Frontière :** Ordre des productions et chaînes d'alias → mots/runs comparables par les interpréteurs.

**Observation :** Pour chaque production d’une classe non uniforme, lr_elect_runs ajoute tous les membres de cette même classe à electTwin. Une classe de K membres occupe ainsi K² IDs identiques par paquets.

**Condition :** Classes de réductions équivalentes dont les mots/watchs d’élection diffèrent ; classes uniformes déjà éliminées.

**Coût ou dette :** K² uint16_t par classe non uniforme, en génération puis dans le header et la table froide ; le stockage partagé pourrait être O(K) plus un index par production.

**Usages et visibilité :** lr_elect.hpp lit le paquet via electTwinAt[p] et electTwinAt[p+1] ; tableaux émis par lr_emit_table et dimensionnés dans LrColdTables.

**Changement proposé :** Introduire un pool de classes et un indice de classe par production, puis adapter le lecteur de paquet. Les offsets adjacents actuels ne permettent pas de simplement réutiliser un ancien début sans changer le contrat.

**Invariants à préserver :** Ordre des membres, uniformité d’élection, RHS propres à chaque production, sentinelles et représentation constexpr générée.

**Validation future — non exécutée :** Comparer élection des twins avec décisions différentes, tailles de tables/sources et compatibilité des tests de tables après migration.

**Preuves dans la révision auditée :**

- [src/engines/cursiv/lr/gen/lr_build_elect.cpp:298](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/gen/lr_build_elect.cpp#L298) — Même groupe repris par chaque membre.
- [src/engines/cursiv/lr/gen/lr_build_elect.cpp:300](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/gen/lr_build_elect.cpp#L300) — Copie de tous les IDs pour chaque production.
- [src/engines/cursiv/lr/runtime/lr_elect.hpp:46](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/runtime/lr_elect.hpp#L46) — Contrat actuel de deux offsets contigus.

<a id="finding-m-lr-program-sparse-build"></a>
### M-LR-PROGRAM-SPARSE-BUILD — La compression des programmes parcourt d’abord toutes les cellules absentes

**optimisation · P2 · candidat · confiance moyenne.** Périmètre principal : **LR-030 — Disposition et encodage des actions et gotos**. Périmètres associés : LR-029, LR-030.

**Frontière :** Actions typées et gardes → programmes numériques et adresses de cellules.

**Observation :** LrProgramLayout remplit une ligne dense par état pour actions puis gotos ; append balaie ensuite tous les blocs de huit valeurs, même quand la ligne ne contient que peu d’arêtes. Le format final peut être compact mais le coût de construction suit les dimensions pleines.

**Condition :** Automates avec beaucoup de symboles et lignes creuses ; avantage dépend de la densité réelle.

**Coût ou dette :** O(S×(T+N)) cellules remises au défaut puis lues en génération, avec O(max(T,N)) scratch dense ; les maps de blocs/runs ajoutent le coût d’internement.

**Usages et visibilité :** LrFinalTable construit le layout une fois après finalisation ; LrProgramRows runtime utilise quatre formes choisies par seal.

**Changement proposé :** Étudier un append de blocs modifiés à partir des lignes déjà ordonnées et un bloc absent partagé ; garder la voie dense pour petites dimensions.

**Invariants à préserver :** Valeurs absentes distinctes actions/gotos, huit cellules et padding, choix exact de seal, compte/offset des ops et bit exact.

**Validation future — non exécutée :** Comparer chaque cellule décodée des quatre formes, dimensions vides et largeurs8/16/32/64 ; mesurer densité/coût de génération avant de retenir l’alternative.

**Preuves dans la révision auditée :**

- [src/engines/cursiv/lr/gen/LrProgramLayout.cpp:33](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/gen/LrProgramLayout.cpp#L33) — Remise à zéro par état des actions.
- [src/engines/cursiv/lr/gen/LrProgramLayout.cpp:76](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/gen/LrProgramLayout.cpp#L76) — Remise au défaut par état des gotos.
- [src/engines/cursiv/lr/gen/LrProgramMatrix.cpp:15](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/gen/LrProgramMatrix.cpp#L15) — Parcours de toute ligne dense.

<a id="finding-m-lr-emit-peek-index"></a>
### M-LR-EMIT-PEEK-INDEX — La génération recherche chaque lookahead dans la collection globale

**optimisation · P3 · candidat · confiance moyenne.** Périmètre principal : **LR-035 — Dispatch, shifts et réductions générés**. Périmètres associés : LR-035.

**Frontière :** Actions/plans d'état → corps C++ des transitions et réductions.

**Observation :** peek_of parcourt a.peeks de zéro à a.npeeks pour retrouver (state,term). emit_dispatch le rappelle pour la projection des cellules, puis la présence et l’émission du peek. Cette collection couvre les états parcourus par lr_peek_oracle et la table est constante pendant l’émission.

**Condition :** Grammaires possédant de nombreuses cellules conflictuelles et de nombreux peeks ; avec npeeks nul ou très petit, le coût reste faible.

**Coût ou dette :** Pour C recherches et P peeks, jusqu’à O(C×P) comparaisons pendant la génération. Le runtime généré ne paie pas cette recherche ; aucun temps gagné mesuré.

**Usages et visibilité :** Helper privé de LrParserEmitter, appelé uniquement par emit_dispatch. lr_peek_oracle remplit la table par état ; LrFinalTable expose ensuite une référence constante au générateur.

**Changement proposé :** Conserver une seule recherche par cellule et étudier un index creux (état,terminal), ou des bornes par état ; exploiter l’ordre existant seulement après avoir garanti qu’aucune finalisation ne le change. Éviter une matrice dense états×terminaux.

**Invariants à préserver :** ID exact du peek, absence représentée par -1, ordre des deux bras et leurs ensembles disjoints ; résultat de partage des dispatchs identique.

**Validation future — non exécutée :** Comparer sources émises et décisions des peeks après modification ; compter recherches/comparaisons sur grammaires à zéro, peu et beaucoup de conflits, puis mesurer le coût de construction de l’index.

**Preuves dans la révision auditée :**

- [src/engines/cursiv/lr/gen/lr_emit_parser.cpp:305](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/gen/lr_emit_parser.cpp#L305) — Recherche globale du couple état/terminal.
- [src/engines/cursiv/lr/gen/lr_emit_parser.cpp:1601](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/gen/lr_emit_parser.cpp#L1601) — Recherche pour le partage des cellules.
- [src/engines/cursiv/lr/gen/lr_emit_parser.cpp:1641](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/gen/lr_emit_parser.cpp#L1641) — Présence puis seconde recherche pour émettre.
- [src/engines/cursiv/lr/gen/lr_build_passes.cpp:249](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/gen/lr_build_passes.cpp#L249) — Remplissage sur tous les états, avant émission.

<a id="finding-m-lr-emit-group-front"></a>
### M-LR-EMIT-GROUP-FRONT — Le regroupement par affinité rescane son front à chaque sélection

**optimisation · P2 · candidat · confiance moyenne.** Périmètre principal : **LR-036 — Fragments, groupes et unités de compilation**. Périmètres associés : LR-036.

**Frontière :** Fragments d'états/réductions → fonctions groupées, déclarations et fichiers C++.

**Observation :** grow_groups incrémente gain et ajoute un voisin à front lors de sa première exposition dans le groupe. Chaque sélection suivante reparcourt tout front, y compris ses sommets déjà placés ; le vecteur est vidé seulement à la fin du groupe. Les sharers sont placés atomiquement et les groupes découlent du poids estimé des lignes.

**Condition :** Automates scindés en plusieurs groupes avec un front d’affinité large. La voie ngroups==1 évite grow_groups ; groupLines réduit généralement le nombre de sélections par groupe, sans borner la largeur des voisins exposés.

**Coût ou dette :** Dans un groupe g, Qg sélections inspectent successivement des fronts Fg,i : coût exact du choix proportionnel à la somme des Fg,i, majoré par Qg×Fg. Une croissance quadratique est possible pour un groupe dont front et nombre de sélections grandissent ensemble ; ce n’est pas un coût quadratique affirmé pour toute grammaire. Les arêtes et leur multiplicité ajoutent le coût de construction et de mise à jour des gains.

**Usages et visibilité :** grow_groups privé, appelé uniquement par plan_groups si ngroups>1. ngroups est ceil(total/groupLines), borné entre 1 et nstates avant retrait des groupes vides ; seuil public par défaut kLrGroupLines=4096, test de découpage avec seuil 8.

**Changement proposé :** Étudier un tas indexé ou des buckets de gain pour les fronts larges, avec suppression des sommets placés ; garder le balayage simple pour les petits fronts. Le tri initial et les gains peuvent rester spécifiques à cet heuristique.

**Invariants à préserver :** Multiplicité des arêtes utilisée comme affinité, choix stable en cas de gain égal, ordre des seeds, placement commun des dispatchs partagés, poids des réductions et plafond cible ; les changements de partition influent sur taille du code, compilation et sauts runtime.

**Validation future — non exécutée :** Compter tailles de fronts, sommets déjà placés revisités et sélections par groupe. Comparer partition, entrées et sauts intergroupes ; mesurer génération, compilation et parse, notamment gros sharers et groupes proches du seuil, avant de retenir une structure plus lourde.

**Preuves dans la révision auditée :**

- [src/engines/cursiv/lr/gen/lr_emit_parser.cpp:2369](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/gen/lr_emit_parser.cpp#L2369) — Heuristique de groupes par affinité.
- [src/engines/cursiv/lr/gen/lr_emit_parser.cpp:2411](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/gen/lr_emit_parser.cpp#L2411) — Ajout unique au front du groupe.
- [src/engines/cursiv/lr/gen/lr_emit_parser.cpp:2416](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/gen/lr_emit_parser.cpp#L2416) — Balayage complet pour chaque sélection.
- [src/engines/cursiv/lr/gen/lr_emit_parser.cpp:2431](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/gen/lr_emit_parser.cpp#L2431) — Effacement seulement à la fin du groupe.
- [src/engines/cursiv/lr/gen/lr_emit_parser.cpp:2461](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/gen/lr_emit_parser.cpp#L2461) — Calcul du nombre de groupes.
- [src/engines/cursiv/lr/gen/lr_emit_parser.hpp:18](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/gen/lr_emit_parser.hpp#L18) — Seuil de lignes cible par groupe.

<a id="finding-m-lr-spec-mark-pairs"></a>
### M-LR-SPEC-MARK-PAIRS — Les marques de spéculation filtrent l’égalité sans éviter la comparaison de toutes les paires

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **LR-043 — Résolution spéculative d'une cellule**. Périmètres associés : LR-043.

**Frontière :** Bras initiaux et pile gelée → bras résolu, fusion ou passage au GSS.

**Observation :** lr_settle_arm calcule un mark pour n>3 puis compare encore chaque i à tous les j suivants. Le hash élimine les marches de pile mais pas le balayage quadratique ; le cap est max(count,64), donc count peut dépasser 64.

**Condition :** Front spéculatif large, notamment nombreuses candidatures lexicales initiales.

**Coût ou dette :** O(N²) comparaisons de marques par round, même lorsque toutes diffèrent.

**Usages et visibilité :** lr_resolve/lr_resolve_cands appelés par les hooks conditionnels LrParser ; helper privé au runtime.

**Changement proposé :** Regrouper les indices par mark puis appliquer lr_shadow_same/lr_trail_meet uniquement dans chaque groupe, avec une voie directe pour les petits fronts.

**Invariants à préserver :** Égalité exacte, ordre des bras, provenance arms et swaps/drop, aucun verdict tiré du hash ; budgets et transfert au GSS.

**Validation future — non exécutée :** Comparer décisions et CST sur ambiguïtés et collisions forcées ; mesurer nombre de paires pour fronts larges.

**Preuves dans la révision auditée :**

- [src/engines/cursiv/lr/runtime/lr_lookahead.hpp:270](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/runtime/lr_lookahead.hpp#L270) — Empreintes
- [src/engines/cursiv/lr/runtime/lr_lookahead.hpp:274](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/runtime/lr_lookahead.hpp#L274) — Toutes les paires
- [src/engines/cursiv/lr/runtime/lr_lookahead.hpp:247](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/runtime/lr_lookahead.hpp#L247) — Largeur initiale non plafonnée à 64

<a id="finding-m-lr-lexical-shift-pairs"></a>
### M-LR-LEXICAL-SHIFT-PAIRS — Deux politiques d’élimination encadrent le même prédicat lexical

**factorisation · P3 · candidat · confiance moyenne.** Périmètre principal : **LR-043 — Résolution spéculative d'une cellule**. Périmètres associés : LR-043, LR-052.

**Frontière :** Bras initiaux et pile gelée → bras résolu, fusion ou passage au GSS.

**Observation :** lr_elected_shift et LrGss::elected_stand réutilisent lr_token_beats mais ne déterminent pas les battus selon la même politique. lr_elected_shift marque dead immédiatement et exclut ensuite les morts des rivaux : l’élimination est progressive. elected_stand calcule chaque beaten contre tous les shifts, y compris ceux précédemment marqués battus, avant de retirer les vertices sans survivant. Le premier filtre aussi shifted, dead et les victimes de longueur nulle ; le second possède un raccourci tokens identiques et la frontière before. La mémorisation paresseuse du poids est similaire, sans preuve d’équivalence de l’ensemble des éliminations.

**Condition :** N shifts rivaux dans la spéculation ou le GSS ; une factorisation n’est intéressante que si elle conserve explicitement leurs politiques de participation distinctes.

**Coût ou dette :** Jusqu’à O(N²) appels au prédicat dans chacun des deux parcours, avec poids calculé au plus une fois par entrée. Le coût de maintenance se limite au bookkeeping ressemblant ; un gain de calcul commun n’est pas établi.

**Usages et visibilité :** lr_elected_shift est consommé par lr_settle_arm ; elected_stand par next_vertex du GSS. Le prédicat est déjà partagé. Ni les shadows ni les vertices ne peuvent être éliminés par une règle commune implicite.

**Changement proposé :** Conserver les deux politiques d’élimination. Si une factorisation reste utile, la limiter à la mémorisation du poids et au prédicat, ou paramétrer explicitement l’ensemble des rivaux actifs à chaque étape. Ne partager l’identification des battus qu’après preuve d’un invariant d’équivalence pour les entrées atteignables.

**Invariants à préserver :** Participation progressive des seuls shadows encore vivants versus comparaison sur tous les shifts du GSS ; ordre des parcours, filtres shifted/dead, victimes de longueur nulle, modes inconnus, symboles identiques et poids. Préserver before, multiplicité des dérivations et protection d’un vertex possédant au moins un survivant.

**Validation future — non exécutée :** Vérifier chaque caller selon sa propre politique sur permutations des rivaux et cas où les exceptions du prédicat empêchent de supposer la transitivité, puis établir leur atteignabilité éventuelle. Comparer décisions et survivants séparément, mesurer avant toute généralisation. Aucune divergence atteignable n’est démontrée dans cet audit.

**Preuves dans la révision auditée :**

- [src/engines/cursiv/lr/runtime/lr_lookahead.hpp:199](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/runtime/lr_lookahead.hpp#L199) — Élection progressive des shadows.
- [src/engines/cursiv/lr/runtime/lr_lookahead.hpp:212](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/runtime/lr_lookahead.hpp#L212) — Les morts cessent de participer aux comparaisons suivantes.
- [src/engines/cursiv/lr/runtime/LrGss.hpp:416](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/runtime/LrGss.hpp#L416) — Élection contre tous les shifts, sans filtrer beaten.
- [src/engines/cursiv/lr/runtime/LrGss.hpp:430](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/runtime/LrGss.hpp#L430) — Protection des vertices ayant un survivant.
- [src/engines/cursiv/lr/runtime/lr_states.hpp:143](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/runtime/lr_states.hpp#L143) — Prédicat commun, exceptions à l’ordre lexical total.

<a id="finding-m-lr-gss-chunks-reuse"></a>
### M-LR-GSS-CHUNKS-REUSE — Un nouvel épisode GSS libère tous les blocs de ses arènes

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **LR-045 — Arènes et index du GSS**. Périmètres associés : LR-045, LR-046.

**Frontière :** Objets/segments et clé d'identité → ID stable avec réutilisation exacte.

**Observation :** LrParser conserve un LrGssGraph par parse, mais seed appelle clear ; les LrGssPool et LrGssRuns libèrent alors chunks/owned, y compris les blocs des petites forêts.

**Condition :** Plusieurs épisodes courts d’ambiguïté dans un parse.

**Coût ou dette :** Allocations/libérations de blocs répétées malgré la réutilisation du graphe ; taille des blocs fixe, pic à évaluer.

**Usages et visibilité :** Pools possédés par Graph/Forest/Rope ; seed au début/reprise d’épisode, aucune API de résultat ne doit pointer vers ces blocs après clear.

**Changement proposé :** Distinguer reset logique et release, retenir éventuellement un petit nombre de blocs avec une politique de plafond ; ne pas retenir tout le maximum historique sans borne.

**Invariants à préserver :** IDs invalidés entre épisodes, références stables pendant croissance, durée de vie des éléments et libération au destructeur.

**Validation future — non exécutée :** Mesurer allocations sur épisodes répétés, puis grand épisode suivi de petits ; comparer CST et pic mémoire.

**Preuves dans la révision auditée :**

- [src/engines/cursiv/lr/runtime/LrGssGraph.cpp:114](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/runtime/LrGssGraph.cpp#L114) — seed réinitialise
- [src/engines/cursiv/lr/runtime/LrGssPool.hpp:37](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/runtime/LrGssPool.hpp#L37) — Blocs libérés
- [src/engines/cursiv/lr/runtime/LrGssRuns.hpp:42](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/runtime/LrGssRuns.hpp#L42) — Runs libérés
- [src/engines/cursiv/lr/runtime/LrParser.hpp:167](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/runtime/LrParser.hpp#L167) — Graphe conservé par parse

<a id="finding-m-lr-gss-accept-paths"></a>
### M-LR-GSS-ACCEPT-PATHS — L’acceptation développe les chemins de pile au-dessus de la forêt empaquetée

**optimisation · P2 · candidat · confiance moyenne.** Périmètre principal : **LR-050 — Acceptation et retour à une pile linéaire**. Périmètres associés : LR-050.

**Frontière :** Sommets validés/acceptants → racine de forêt ou reprise du driver.

**Observation :** accept utilise each_path pour chaque chemin descendant et construit un vector inversé de forests par chemin avant de l’ajouter comme alternative de la racine. La forêt reste empaquetée plus bas, mais cette couche matérialise chaque chemin.

**Condition :** Graphe de piles présentant de nombreux chemins, par exemple une succession de losanges ; atteignabilité par une grammaire à confirmer.

**Coût ou dette :** Travail proportionnel au nombre de chemins multiplié par leur longueur et allocation temporaire par chemin ; ce nombre peut être exponentiel dans un DAG de losanges.

**Usages et visibilité :** accept appelé par close/hand_back ; each_path est aussi utilisé avec limite dans sources, qui possède déjà une stratégie bornée distincte.

**Changement proposé :** Étudier une racine de forêt construite par vertex avec suffixes partagés, ou au minimum réutiliser le buffer inversé ; ne pas appliquer aveuglément la limite de sources à l’acceptation.

**Invariants à préserver :** Toutes les dérivations admises et leur ordre lexical/écrit, étapes finales, seed et absence de cycle.

**Validation future — non exécutée :** Établir d’abord des grammaires donnant ce graphe ; compter chemins/allocations puis comparer élection/CST.

**Preuves dans la révision auditée :**

- [src/engines/cursiv/lr/runtime/LrGssGraph.cpp:827](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/runtime/LrGssGraph.cpp#L827) — Acceptation
- [src/engines/cursiv/lr/runtime/LrGssGraph.cpp:830](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/runtime/LrGssGraph.cpp#L830) — Copie inverse par chemin
- [src/engines/cursiv/lr/runtime/LrGssGraph.cpp:785](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/runtime/LrGssGraph.cpp#L785) — Appels pour acceptations

<a id="finding-m-lr-gss-readpast-propagation"></a>
### M-LR-GSS-READPAST-PROPAGATION — read_past refait un balayage global des arcs jusqu’au point fixe

**optimisation · P3 · candidat · confiance moyenne.** Périmètre principal : **LR-051 — Sélection des sources de réparation**. Périmètres associés : LR-051.

**Frontière :** Lectures gelées et chemins GSS → sources classées puis piles de départ.

**Observation :** read_past parcourt tous les arcs à chaque round, même si seules quelques sorties changent, puis tous les vertices pour le maximum.

**Condition :** Graphe important et propagation nécessitant plusieurs rounds.

**Coût ou dette :** O(A×I+V) pour A arcs, V vertices et I rounds ; I dépend de l’ordre des arcs et de leur évolution.

**Usages et visibilité :** sources appelle read_past avant sélection ; les Ends sont monotones entre clear, fournissant une base pour propagation incrémentale.

**Changement proposé :** Construire/maintenir une adjacence sortante et une worklist des vertices modifiés ; mesurer si le coût mémoire vaut les scans évités.

**Invariants à préserver :** Ordre des trois derniers vrais tokens, fusion composante par composante, valeurs monotones et effacement au nouvel épisode.

**Validation future — non exécutée :** Comparer Ends et sélection des sources, compter arcs visités sur chaînes/losanges et épisodes courts.

**Preuves dans la révision auditée :**

- [src/engines/cursiv/lr/runtime/LrGssGraph.cpp:841](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/runtime/LrGssGraph.cpp#L841) — Point fixe
- [src/engines/cursiv/lr/runtime/LrGssGraph.cpp:845](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/runtime/LrGssGraph.cpp#L845) — Rounds globaux

<a id="finding-m-lr-cactus-siblings"></a>
### M-LR-CACTUS-SIBLINGS — L’internement cactus parcourt linéairement les enfants d’un même parent

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **LR-053 — Piles cactus et retour arrière d'une recherche**. Périmètres associés : LR-053.

**Frontière :** Frames source et pushes/pop de candidats → piles persistantes réutilisables.

**Observation :** intern recherche state dans une chaîne de siblings et fait un move-to-front ; rewind recherche aussi le lien à retirer. Le compteur _stepped facture ce travail mais ne l’élimine pas.

**Condition :** Parent recevant K états enfants distincts ou lectures alternant des enfants nombreux.

**Coût ou dette :** O(K²) recherches cumulées pour K insertions distinctes sous ce parent ; rewind peut rescanner les siblings après réordonnancement.

**Usages et visibilité :** LrCactus utilisé seulement par réparation LR via LrCactusClimb, add, lr_search_trim ; ses futurs reposent sur IDs canoniques.

**Changement proposé :** Garder la chaîne pour petits degrés ; étudier un index (parent,state) au-delà d’un seuil, avec journal ou effacement compatible rewind.

**Invariants à préserver :** Identité canonique, move-to-front non sémantique, marks ne retirant aucun survivant, comptage de travail et chaînes begin distinctes.

**Validation future — non exécutée :** Comparer configurations/gestes et nombre _stepped sur forte largeur ; exercer rewind après hits réordonnés.

**Preuves dans la révision auditée :**

- [src/engines/cursiv/lr/runtime/LrCactus.hpp:94](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/runtime/LrCactus.hpp#L94) — Internement
- [src/engines/cursiv/lr/runtime/LrCactus.hpp:56](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/runtime/LrCactus.hpp#L56) — Retrait par recherche
- [src/engines/cursiv/lr/runtime/LrCactus.hpp:116](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/runtime/LrCactus.hpp#L116) — Coût facturé

<a id="finding-m-lr-bound-candidate-index"></a>
### M-LR-BOUND-CANDIDATE-INDEX — Les candidats de borne sont dédoublonnés par balayage du résultat croissant

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **LR-057 — Borne inférieure par programmation dynamique**. Périmètres associés : LR-057.

**Frontière :** Position, terminal précédent et progression → minimum de gestes encore dus.

**Observation :** lr_search_ahead::add compare un Token à tous ceux déjà collectés ; lr_search_misread refait cette démarche sur (term,end). Cela diffère du TypoCandidates indexé utilisé par le rescan normal.

**Condition :** M candidats fournis par plusieurs modes à une même position.

**Coût ou dette :** O(M²) comparaisons dans le pire cas ; répété aux positions consultées par la DP.

**Usages et visibilité :** Ces collections sont privées à lr_search_bound, consommées par LrSearchOwes et mémorisées dans ahead/misread.

**Changement proposé :** Ajouter une admission indexée conservant le vector dans l’ordre d’émission ; réutiliser une structure de clé exacte, sans imposer le classement/coût de TypoCandidates à misread.

**Invariants à préserver :** Identité complète Token pour ahead, seulement (term,end) pour misread, première occurrence, cut/unread et stabilité des références des mémos.

**Validation future — non exécutée :** Comparer borne et candidats sur modes partageant des tokens et coûts typo différents ; compter les comparaisons.

**Preuves dans la révision auditée :**

- [src/engines/cursiv/lr/runtime/lr_search_bound.hpp:204](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/runtime/lr_search_bound.hpp#L204) — Dédoublonnage ahead
- [src/engines/cursiv/lr/runtime/lr_search_bound.hpp:306](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/runtime/lr_search_bound.hpp#L306) — Dédoublonnage misread
- [src/engines/cursiv/shared/runtime/TypoCandidates.hpp:25](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/shared/runtime/TypoCandidates.hpp#L25) — Index déjà présent pour autre contrat

<a id="finding-m-lr-caret-scan-index"></a>
### M-LR-CARET-SCAN-INDEX — Les mémos lexicaux du caret recherchent leurs clés dans des vectors

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **LR-064 — Lecture lexicale autour du caret**. Périmètres associés : LR-064.

**Frontière :** Frames et zone de texte → candidats lexicaux, franchissement des trivia et refus.

**Observation :** lex parcourt _scanned sur (mode,from,inside) et trivia_to parcourt _crossed sur (mode,inside,at,to). Les résultats sont bien réutilisés mais chaque lookup relit toutes les clés précédentes.

**Condition :** Complétion sur les structures de grande taille décrites.

**Coût ou dette :** Q requêtes sur K clés peuvent effectuer O(Q×K) comparaisons ; création de K clés distinctes O(K²).

**Usages et visibilité :** CaretStacks privé, appelé par reading et essais de candidats ; forget_scans fixe la durée de ces caches.

**Changement proposé :** Ajouter un index local clé exacte→offset des vectors, éventuellement après seuil pour conserver les petits cas simples.

**Invariants à préserver :** Buffers référencés par indices/spans, réinitialisation à forget_scans, refus dépendant du head hors mémo lorsque nécessaire ; aucune clé réduite au hash.

**Validation future — non exécutée :** Vérifier ultérieurement mêmes NextFeature et ordre sur CST propres/réparés, modes multiples et conditions ; mesurer opérations et allocations.

**Preuves dans la révision auditée :**

- [src/engines/cursiv/lr/runtime/LrCaretTable.cpp:1491](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/runtime/LrCaretTable.cpp#L1491) — Lookup scan
- [src/engines/cursiv/lr/runtime/LrCaretTable.cpp:1629](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/runtime/LrCaretTable.cpp#L1629) — Lookup trivia
- [src/engines/cursiv/lr/runtime/LrCaretTable.cpp:1417](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/runtime/LrCaretTable.cpp#L1417) — Portée du cache

<a id="finding-m-lr-caret-repair-index"></a>
### M-LR-CARET-REPAIR-INDEX — Chaque complétion d’un CST réparé rescane tous les records pour indexer les réparations

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **LR-065 — Rejeu du CST vers le caret**. Périmètres associés : LR-065.

**Frontière :** CST propre/réparé et offset → préfixe de piles reconstruit et événements d'indentation.

**Observation :** CaretReplay parcourt de 0 à cst.size dès que cst.recovered() est vrai afin de remplir _repairs, même si le replay demandé ne couvre qu’un petit préfixe. clean utilise ensuite lower_bound sur ce vector.

**Condition :** Complétion sur les structures de grande taille décrites.

**Coût ou dette :** O(N) lecture de records et O(R) stockage par requête, même pour caret proche du début.

**Usages et visibilité :** CaretReplay local à lr_next_features_at ; _repairs privé, consommé par clean.

**Changement proposé :** Limiter la collecte au domaine réellement nécessaire et au token suivant, ou conserver un index avec le CST si ses autres consommateurs le justifient.

**Invariants à préserver :** clean doit voir la réparation du sous-arbre et celle du token suivant ; lifetime du CST, régions abandonnées et EOF.

**Validation future — non exécutée :** Vérifier ultérieurement mêmes NextFeature et ordre sur CST propres/réparés, modes multiples et conditions ; mesurer opérations et allocations.

**Preuves dans la révision auditée :**

- [src/engines/cursiv/lr/runtime/LrCaretTable.cpp:1901](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/runtime/LrCaretTable.cpp#L1901) — Garde puis scan total
- [src/engines/cursiv/lr/runtime/LrCaretTable.cpp:1903](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/runtime/LrCaretTable.cpp#L1903) — Index reconstruit
- [src/engines/cursiv/lr/runtime/LrCaretTable.cpp:1930](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/runtime/LrCaretTable.cpp#L1930) — Consommateur de l’index

<a id="finding-m-lr-caret-sample-union"></a>
### M-LR-CARET-SAMPLE-UNION — L’union des samples est recalculée pour chaque contexte de collage du même terminal

**optimisation · P3 · établi · confiance forte.** Périmètre principal : **LR-066 — Essai de candidats et projection des features**. Périmètres associés : LR-066.

**Frontière :** Piles au caret, spellings et sites attendus → ensemble des NextFeature.

**Observation :** words_fit parcourt tous les modes pour refaire read|=sampled_in(mode,term) à chaque couple reading/mode/passes absent de glue ; la même union est reconstruite avant les essais de mots conditionnels.

**Condition :** Complétion sur les structures de grande taille décrites.

**Coût ou dette :** O(G×M) accès aux cellules pour G contextes de collage et M modes d’un terminal, même après remplissage du cache.

**Usages et visibilité :** Lambdas words_fit et branche conditionnelle dans la boucle term de lr_next_features_at ; sampled_in est invariant pour cette table/terminal.

**Changement proposé :** Calculer paresseusement une seule union locale par terminal puis la réutiliser dans words_fit et la branche des conditions.

**Invariants à préserver :** Ne pas supprimer les vérifications fits par contexte, le masque des samples ni l’ordre des mots ; pas de cache partagé supplémentaire nécessaire.

**Validation future — non exécutée :** Vérifier ultérieurement mêmes NextFeature et ordre sur CST propres/réparés, modes multiples et conditions ; mesurer opérations et allocations.

**Preuves dans la révision auditée :**

- [src/engines/cursiv/lr/runtime/LrCaretTable.cpp:2406](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/runtime/LrCaretTable.cpp#L2406) — Calcul dans le contexte de collage
- [src/engines/cursiv/lr/runtime/LrCaretTable.cpp:2410](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/runtime/LrCaretTable.cpp#L2410) — Union des modes
- [src/engines/cursiv/lr/runtime/LrCaretTable.cpp:2468](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/runtime/LrCaretTable.cpp#L2468) — Deuxième consommation

<a id="finding-m-lr-caret-feature-admission"></a>
### M-LR-CARET-FEATURE-ADMISSION — Les shifts et features déjà émis sont recherchés linéairement

**optimisation · P3 · établi · confiance forte.** Périmètre principal : **LR-066 — Essai de candidats et projection des features**. Périmètres associés : LR-066.

**Frontière :** Piles au caret, spellings et sites attendus → ensemble des NextFeature.

**Observation :** Le callback final recherche chaque shift dans emitted et chaque NextFeature dans le suffixe out[mine..]. Les doublons peuvent venir de stacks, landings ou spellings différents.

**Condition :** Complétion sur les structures de grande taille décrites.

**Coût ou dette :** O(S²+F²) comparaisons au pire pour S shifts et F features d’un terminal.

**Usages et visibilité :** Callback stacks.next ; expectation rows trouvées par lower_bound, out est un paramètre append.

**Changement proposé :** Conserver l’ordre des vectors mais ajouter un index d’admission exact, éventuellement après seuil.

**Invariants à préserver :** Dédoublonnage à partir de mine, ordre de première émission, reset lors des samples et préfixe out conservé.

**Validation future — non exécutée :** Vérifier ultérieurement mêmes NextFeature et ordre sur CST propres/réparés, modes multiples et conditions ; mesurer opérations et allocations.

**Preuves dans la révision auditée :**

- [src/engines/cursiv/lr/runtime/LrCaretTable.cpp:2445](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/runtime/LrCaretTable.cpp#L2445) — Lookup de shift
- [src/engines/cursiv/lr/runtime/LrCaretTable.cpp:2461](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/runtime/LrCaretTable.cpp#L2461) — Lookup de feature

<a id="finding-m-lr-sample-cache-dense"></a>
### M-LR-SAMPLE-CACHE-DENSE — Le premier sample alloue la matrice complète modes×terminaux

**optimisation · P3 · candidat · confiance moyenne.** Périmètre principal : **LR-067 — Cache partagé des mots d'échantillonnage**. Périmètres associés : LR-067.

**Frontière :** Couple mode/terminal → masque immuable des exemples lexicaux admis.

**Observation :** LrSampleReads::cells alloue et zéro-initialise count atomiques ; sampled_in fournit modeTight.size()*terms.size(). Une requête n’utilisant que quelques cellules porte donc le coût initial de toute la matrice.

**Condition :** Complétion sur les structures de grande taille décrites.

**Coût ou dette :** O(M×T) mémoire et initialisation ; deux premières requêtes concurrentes peuvent allouer chacune la matrice avant le CAS.

**Usages et visibilité :** Cache possédé par LrLanguageParser, uniquement lié lorsque drive.condText existe ; lifetime du parser, pas de chaque requête.

**Changement proposé :** Évaluer une allocation par ligne ou bloc de modes, ou conserver la matrice si sa densité réelle justifie son accès direct ; éviter une map atomique complexe sans mesure.

**Invariants à préserver :** Publication acquire/release, accès concurrent aux cellules, valeur connue zéro via bit haut, destruction sans lecteur actif et tailles fixes de table.

**Validation future — non exécutée :** Vérifier ultérieurement mêmes NextFeature et ordre sur CST propres/réparés, modes multiples et conditions ; mesurer opérations et allocations.

**Preuves dans la révision auditée :**

- [src/engines/cursiv/lr/runtime/LrCaretTable.cpp:2076](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/runtime/LrCaretTable.cpp#L2076) — Allocation totale
- [src/engines/cursiv/lr/runtime/LrCaretTable.cpp:2352](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/runtime/LrCaretTable.cpp#L2352) — Dimension produit
- [src/engines/cursiv/lr/runtime/LrLanguageParser.hpp:45](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/runtime/LrLanguageParser.hpp#L45) — Cache seulement avec conditions

<a id="finding-m-lr-bitset-members"></a>
### M-LR-BITSET-MEMBERS — L’énumération par take rescane les mots vides depuis le début

**optimisation · P3 · établi · confiance forte.** Périmètre principal : **LR-069 — Opérations sur bitsets statiques et stockage des ensembles**. Périmètres associés : LR-069.

**Frontière :** Indices de bits et ensembles de terminaux → opérations de bitset et lecture d’un ensemble partagé par plusieurs états.

**Observation :** take appelle first, qui recommence à w[0] à chaque membre extrait. each_accept copie un set puis répète take ; lorsque les premiers mots sont déjà vidés, ils sont revisités pour tous les membres des mots suivants.

**Condition :** Sets étendus et plusieurs membres situés dans les derniers mots, ou énumérations denses répétées.

**Coût ou dette :** Jusqu’à O(M×W) inspections de mots pour M membres sur W mots, contre O(W+M) pour un curseur de mot ; copie complète du set à l’entrée de each_accept.

**Usages et visibilité :** LrFrozenTable::each_accept et code LR consomment les sets ; LrBitSet sert aussi aux origines spéculatives, dont le contrat mutable doit rester distinct.

**Changement proposé :** Ajouter une plage members/itérateur non destructif avec curseur de mot, et migrer les parcours purs. Conserver take pour les vrais worksets mutables.

**Invariants à préserver :** Ordre croissant des bits, rank correspondant aux pools accBound, borne logique Bits et absence de bits de padding.

**Validation future — non exécutée :** Vérifier ensembles vides, mots multiples, dernier mot partiel et acceptations avec accStand ; compter inspections de mots avant/après.

**Preuves dans la révision auditée :**

- [src/engines/cursiv/lr/table/LrBitSet.hpp:53](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/table/LrBitSet.hpp#L53) — Recherche depuis le mot zéro.
- [src/engines/cursiv/lr/table/LrBitSet.hpp:61](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/table/LrBitSet.hpp#L61) — take recommence first à chaque fois.
- [src/engines/cursiv/lr/table/LrFrozenTable.hpp:123](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/table/LrFrozenTable.hpp#L123) — Copie puis énumération par take.

<a id="finding-m-lsp-notification-builds"></a>
### M-LSP-NOTIFICATION-BUILDS — Les rafales déjà en file déclenchent un build par notification

**optimisation · P2 · candidat · confiance moyenne.** Périmètre principal : **LSP-003 — Admission des notifications**. Périmètres associés : LSP-003, LSP-004, LSP-011, LSP-012, LSP-014.

**Frontière :** Notification reçue → tâche numérotée dans la file du serveur

**Observation :** run consomme chaque tâche opaque ; l’annulation à post ne marque pas comme annulée une future écriture qui n’existait pas encore.

**Condition :** Plusieurs changements arrivent pendant une longue tâche puis cessent ; la file se vide ensuite.

**Coût ou dette :** Jusqu’à une chaîne complète update/build par notification en retard, et allocations de closures ; coût dépend de ce qui peut reprendre.

**Usages et visibilité :** NotificationQueue mélange mutations, configuration et callbacks ; les frontières posted/served ordonnent les requêtes.

**Changement proposé :** Candidat : représenter explicitement les notifications de texte et regrouper seulement les mutations adjacentes compatibles, puis construire à la frontière demandée.

**Invariants à préserver :** Installer tous les edits séquentiels et versions, conserver FIFO observable et fences des requêtes, ne pas fusionner callbacks arbitraires ni supprimer actions Durable.

**Validation future — non exécutée :** Vérifier rafales avec requêtes intercalées, changements multi-doc, close/open et shutdown ; mesurer nombre de builds.

**Preuves dans la révision auditée :**

- [src/lsp/cursiv/runtime/LanguageServerRuntime.cpp:170](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/lsp/cursiv/runtime/LanguageServerRuntime.cpp#L170) — task = std::move(_tasks.front());
- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:1377](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L1377) — const bool moved = mutate();

<a id="finding-m-lsp-defer-callback"></a>
### M-LSP-DEFER-CALLBACK — Mutualiser la remise des callbacks sortants

**factorisation · P3 · établi · confiance forte.** Périmètre principal : **LSP-009 — Requêtes sortantes et callbacks client**. Périmètres associés : LSP-009.

**Frontière :** Appel LanguageClient → message sortant puis callback dans le contexte serveur

**Observation :** Deux callbacks répètent gestion de then/defer et capture movable du résultat ; la configuration transporte un vector, le custom une Value.

**Condition :** Évolution de l’ordonnancement ou du lifetime des callbacks client.

**Coût ou dette :** Duplication de plomberie et risque de divergence ; aucune amélioration runtime chiffrée.

**Usages et visibilité :** ServerEndpointLanguageClient privé à l’adaptation ; conserver API publique callbacks owning.

**Changement proposé :** Petit helper template interne qui remet un résultat à then via Defer, sans changer les wrappers d’envoi/session.

**Invariants à préserver :** Résultat possédé jusqu’à exécution, callback unique et exécution directe quand defer absent ; aucune attente du read loop.

**Validation future — non exécutée :** Vérifier callbacks nuls, deferred/direct et fermeture de session ; comparaison statique du comportement.

**Preuves dans la révision auditée :**

- [src/lsp/cursiv/runtime/ServerEndpointLanguageClient.cpp:71](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/lsp/cursiv/runtime/ServerEndpointLanguageClient.cpp#L71) — if (!defer) {
- [src/lsp/cursiv/runtime/ServerEndpointLanguageClient.cpp:75](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/lsp/cursiv/runtime/ServerEndpointLanguageClient.cpp#L75) — defer([then, result = std::move(result)]() mutable

<a id="finding-m-lsp-config-unchanged"></a>
### M-LSP-CONFIG-UNCHANGED — Les configurations poussées identiques relancent la validation

**optimisation · P2 · candidat · confiance moyenne.** Périmètre principal : **LSP-016 — Acquisition et cache de configuration**. Périmètres associés : LSP-016.

**Frontière :** Capacités client et sections déclarées → valeurs disponibles dans ConfigurationProvider

**Observation :** Contrairement au pull, le push ne compare pas la valeur existante avant de notifier les listeners.

**Condition :** Client répète settings complets ou réémet une section inchangée.

**Coût ou dette :** O(D langue) resets/build, plus copie JSON et réveils, pour une valeur équivalente.

**Usages et visibilité :** ConfigurationSectionListener public peut actuellement compter chaque notification, donc changement contractuel à préciser.

**Changement proposé :** Candidat : retourner un indicateur changed de l’installation de section et ne revalider que les changements utiles, au minimum au listener du builder.

**Invariants à préserver :** Revision de push doit toujours protéger contre une réponse pull plus vieille ; les consommateurs demandant chaque événement doivent être documentés.

**Validation future — non exécutée :** Vérifier push identique pendant pull, changement unrelated et suppression section ; mesurer nombre de validations.

**Preuves dans la révision auditée :**

- [src/lsp/cursiv/workspace/DefaultConfigurationProvider.cpp:206](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/lsp/cursiv/workspace/DefaultConfigurationProvider.cpp#L206) — updateSectionConfiguration(section, value);
- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:759](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L759) — builder.resetToState(

<a id="finding-m-lsp-config-per-document"></a>
### M-LSP-CONFIG-PER-DOCUMENT — Les mêmes options de langue sont reconstruites par document

**optimisation · P2 · candidat · confiance moyenne.** Périmètre principal : **LSP-017 — Configuration et catégories de validation**. Périmètres associés : LSP-017.

**Frontière :** Valeurs de configuration → options du prochain build et revalidation concernée

**Observation :** À chaque boucle, getConfiguration copie le JSON sous mutex puis validation_categories parcourt les noms et le registre ; la réponse est identique pour une langue fixée.

**Condition :** Build de D documents de L langues, D≫L.

**Coût ou dette :** O(D) copies JSON/allocations et conversions des mêmes catégories ; répétition possible dans prepare puis build.

**Usages et visibilité :** Les options explicites inachevées restent prioritaires ; configurationProvider est une interface personnalisable.

**Changement proposé :** Candidat : mémo par langue/révision de configuration pendant une opération de build, ou vérifier demande inachevée avant conversion.

**Invariants à préserver :** Ne pas remplacer une demande explicite ni réutiliser après mise à jour de registre/config ; hooks custom document-dépendants doivent rester possibles.

**Validation future — non exécutée :** Vérifier catégories custom, configure en rafale et demandes interrompues ; compter accès JSON/allocations.

**Preuves dans la révision auditée :**

- [src/lsp/cursiv/workspace/LspDocumentBuilder.cpp:50](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/lsp/cursiv/workspace/LspDocumentBuilder.cpp#L50) — getConfiguration(languageId, "validation")
- [src/lsp/cursiv/workspace/LspDocumentBuilder.cpp:53](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/lsp/cursiv/workspace/LspDocumentBuilder.cpp#L53) — validation_categories(*validation

<a id="finding-m-protocol-document-route"></a>
### M-PROTOCOL-DOCUMENT-ROUTE — Partage partiel possible de l’acquisition documentaire LSP/MCP

**factorisation · P2 · candidat · confiance moyenne.** Périmètre principal : **LSP-019 — Prérequis de phase et langue du document**. Périmètres associés : LSP-019, MCP-004, MCP-011, MCP-015.

**Frontière :** Requête de service sur une URI → lecture stable au niveau document ou workspace exigé

**Observation :** Deux implémentations maintiennent lookup ID, waitUntil, réacquisition, contrôle state et sélection de langue. Elles divergent sur portée, résultat et erreurs.

**Condition :** Ajout d’un rung, gestion de suppression/annulation ou évolution des garanties Immediate.

**Coût ou dette :** Maintenance parallèle d’une frontière concurrente sensible ; pas de gain mesuré promis.

**Usages et visibilité :** LSP dépend de ServiceRequirement et slots optionnels, MCP seulement CoreServices ; dépendance core→LSP interdite.

**Changement proposé :** Extraire un noyau core retournant un statut d’admission explicite, paramétré par portée documentaire/workspace ; garder mapping wire et sélection des providers aux couches.

**Invariants à préserver :** Attendre hors read, state vérifié après admission, Normal pour snapshot workspace, Immediate seulement après phase suffisante ; ne pas confondre échec et résultat vide.

**Validation future — non exécutée :** Vérifier changement/suppression entre wait/read, cancellation, build failure et langue reroutée ; comparer tous les callers.

**Preuves dans la révision auditée :**

- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:920](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L920) — workspace ? WorkspaceLock::ReadPriority::Normal
- [src/mcp/cursiv/McpToolKit.cpp:113](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/mcp/cursiv/McpToolKit.cpp#L113) — document->state.load() < rung

<a id="finding-m-lsp-completion-render-dup"></a>
### M-LSP-COMPLETION-RENDER-DUP — Des items équivalents sont rendus avant la dédup finale

**optimisation · P2 · candidat · confiance moyenne.** Périmètre principal : **LSP-025 — Items, documentation et modifications**. Périmètres associés : LSP-024, LSP-025.

**Frontière :** Candidat de référence ou mot-clé → CompletionItem final dédupliqué

**Observation :** Le seen des références est local à completionForCrossReference ; deux NextFeature peuvent rendre les mêmes noms/docs et seules les clés finales suppriment le doublon.

**Condition :** Frontière de complétion proposant plusieurs sites de référence compatibles ou alternatives répétées.

**Coût ou dette :** Rendu de documentation, allocations de CompletionItem et text edits répétés pour des items éliminés.

**Usages et visibilité :** Providers custom peuvent changer la clé finale selon le site ; ne pas dédupliquer les NextFeature au seul property.

**Changement proposé :** Candidat : cache de rendu par requête pour le défaut ou clé d’équivalence explicite avant création, en gardant la dédup finale.

**Invariants à préserver :** Ordre du premier item, distinction kind/detail et hook create/fill ; contextes différents peuvent proposer des edits différents.

**Validation future — non exécutée :** Vérifier alternatives, narrowings distincts, labels personnalisés et documentation cross-language ; mesurer allocations.

**Preuves dans la révision auditée :**

- [src/lsp/cursiv/completion/DefaultCompletionProvider.cpp:138](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/lsp/cursiv/completion/DefaultCompletionProvider.cpp#L138) — for (const NextFeature& next : context.features)
- [src/lsp/cursiv/completion/DefaultCompletionProvider.cpp:461](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/lsp/cursiv/completion/DefaultCompletionProvider.cpp#L461) — getReferenceDocumentation(candidate)

<a id="finding-m-lsp-reference-uri-repeat"></a>
### M-LSP-REFERENCE-URI-REPEAT — Les réponses de références reparsent l’URI à chaque occurrence

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **LSP-028 — Références et occurrences locales**. Périmètres associés : LSP-028, LSP-029.

**Frontière :** Position de référence ou déclaration → emplacements de références ou surlignages du document

**Observation :** reference_location copie getDocumentUri, charge le shared_ptr puis parse la même URI pour chaque row d’un document.

**Condition :** Beaucoup de références/renommages dans quelques fichiers.

**Coût ou dette :** O(R×longueur URI) parsing/copies et R acquisitions shared_ptr ; même URI finale répétée dans les Locations demeure nécessaire au wire.

**Usages et visibilité :** Rows de collect_references_per_declaration sont triées par sourceDocumentId ; helper public peut rester unitaire.

**Changement proposé :** Ajouter un adaptateur de lot ou un petit contexte du document courant avec URI parsée et TextDocument pour la conversion.

**Invariants à préserver :** Chaque range utilise le bon document/version ; handles ne doivent pas sortir du read autorisé, ordre et dédup des rows conservés.

**Validation future — non exécutée :** Vérifier changements de source, documents absents et URI virtuelles ; mesurer lots de références.

**Preuves dans la révision auditée :**

- [src/lsp/cursiv/navigation/ReferenceLocation.cpp:22](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/lsp/cursiv/navigation/ReferenceLocation.cpp#L22) — const std::string uri = store.getDocumentUri
- [src/lsp/cursiv/navigation/ReferenceLocation.cpp:28](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/lsp/cursiv/navigation/ReferenceLocation.cpp#L28) — location.uri = lsp::Uri::parse(uri);

<a id="finding-m-lsp-named-recompute"></a>
### M-LSP-NAMED-RECOMPUTE — Outline et coloration reconstruisent les mêmes tables de noms

**optimisation · P2 · candidat · confiance moyenne.** Périmètre principal : **LSP-030 — Hiérarchie de symboles du document**. Périmètres associés : LSP-030, LSP-039, LSP-040.

**Frontière :** AST nommé → DocumentSymbol avec type et plages

**Observation :** Chaque demande alloue des tableaux à la taille du CST/AST et recalcule les NameNodes/owners ; range semantic le fait aussi pour tout le document.

**Condition :** Éditeur demande outline et tokens, puis plusieurs ranges sans nouveau parse.

**Coût ou dette :** O(C+A) structures et appels NameProvider par requête ; mémoire temporaire O(C+A).

**Usages et visibilité :** Helper NamedNodes commun déjà présent ; pas d’API qui certifie NameProvider pur par build.

**Changement proposé :** Candidat : snapshot NamedNodes par document/revision à invalidation explicite, ou cache opt-in pour providers dont le nom ne dépend que du parse.

**Invariants à préserver :** Nodes et CstNodeViews expirent au reset ; langue custom et liens/config pouvant affecter les noms ; thread-safe publication.

**Validation future — non exécutée :** Vérifier rename/rebuild/langue custom, ranges successives et deux demandes concurrentes ; mesurer allocations.

**Preuves dans la révision auditée :**

- [src/lsp/cursiv/symbols/NamedNodes.cpp:32](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/lsp/cursiv/symbols/NamedNodes.cpp#L32) — std::vector opens
- [src/lsp/cursiv/symbols/NamedNodes.cpp:50](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/lsp/cursiv/symbols/NamedNodes.cpp#L50) — std::vector ownerAt

<a id="finding-m-lsp-format-keyword-first"></a>
### M-LSP-FORMAT-KEYWORD-FIRST — Le premier keyword est sélectionné après collecte de tous ses matches

**optimisation · P3 · établi · confiance forte.** Périmètre principal : **LSP-035 — Sélection des régions et actions**. Périmètres associés : LSP-035.

**Frontière :** Nœud à formater et sélecteurs déclaratifs → actions attachées aux régions choisies

**Observation :** Le défaut keyword(keyword,0) alloue deux listes de matches avant le vector d’un seul record, malgré find_node_for_keyword index zéro déjà disponible.

**Condition :** Règles de formatage fréquentes sélectionnant une accolade ou un séparateur dans une grande construction.

**Coût ou dette :** Scan intégral du sous-arbre et allocations O(K matches) pour une seule occurrence.

**Usages et visibilité :** NodeFormatter public template ; keyword_nodes reste utile à keywords et aux callers demandant toutes les occurrences.

**Changement proposé :** Ajouter seulement le fast path index==0 utilisant le helper core ; laisser la politique actuelle des autres indices intacte.

**Invariants à préserver :** Bornes hors range : keyword(index>0) retourne vide, tandis que helper core clamp ; ownership du FormattingRegion conservé.

**Validation future — non exécutée :** Comparer premier/absent/répété et indice trop grand ; vérifier constructions collapsed et macros.

**Preuves dans la révision auditée :**

- [src/lsp/cursiv/formatting/AbstractFormatter.hpp:117](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/lsp/cursiv/formatting/AbstractFormatter.hpp#L117) — if (index >= found.size())
- [src/core/cursiv/syntax-tree/CstUtils.cpp:165](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/syntax-tree/CstUtils.cpp#L165) — return first_of<false>(

<a id="finding-m-lsp-format-edit-copies"></a>
### M-LSP-FORMAT-EDIT-COPIES — La finalisation du formatage recopie les textes d’édition deux fois

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **LSP-036 — Arbitrage des espaces et retraits**. Périmètres associés : LSP-036, LSP-037, LSP-038, MCP-019.

**Frontière :** Actions collectées sur les frontières CST → TextEdit d’espacement et d’indentation

**Observation :** edits est reçu par valeur puis chaque PendingEdit const est copié vers filtered ; chaque newText est copié à nouveau vers le TextEdit final.

**Condition :** Nombreuses éditions ou remplacements de commentaires volumineux.

**Coût ou dette :** Deux copies/allocation potentielles de chaque texte retenu, plus copies de candidats éliminés tardivement.

**Usages et visibilité :** Toutes ces structures sont locales à finalize_edits ; aucun appelant n’observe leur état après passage.

**Changement proposé :** Itérer en mutable et déplacer vers filtered, puis déplacer newText vers result après comparaison no-op ; réserver filtered si justifié.

**Invariants à préserver :** Même ordre stable, later-wins des overlaps et duplicates exacts ; ne pas comparer un string déjà déplacé.

**Validation future — non exécutée :** Vérifier overlaps imbriqués, zéro-width, no-op CRLF et grosses réécritures de commentaires.

**Preuves dans la révision auditée :**

- [src/lsp/cursiv/formatting/FormattingEngine.cpp:474](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/lsp/cursiv/formatting/FormattingEngine.cpp#L474) — std::vector<PendingEdit> filtered;
- [src/lsp/cursiv/formatting/FormattingEngine.cpp:487](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/lsp/cursiv/formatting/FormattingEngine.cpp#L487) — for (const PendingEdit& edit : filtered)

<a id="finding-m-lsp-action-reconvert"></a>
### M-LSP-ACTION-RECONVERT — Le matching du même diagnostic est refait pour chaque quickfix

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **LSP-041 — Actions issues des diagnostics et versions**. Périmètres associés : LSP-022, LSP-041.

**Frontière :** Diagnostics du client et actions métier → CodeAction applicable à une révision

**Observation :** Chaque make_action cherche à nouveau le lsp::Diagnostic correspondant ; matches_diagnostic reconvertit le payload core lorsque les champs simples concordent.

**Condition :** Plusieurs fixes pour un même diagnostic, ou beaucoup de diagnostics client comparables.

**Coût ou dette :** Comparaisons répétées O(A×C) et copies JSON par candidats compatibles, A fixes/C diagnostics client.

**Usages et visibilité :** La sélection de diagnostic appartient au groupe de fixes ; les ranges d’édition et titres appartiennent à chaque fix.

**Changement proposé :** Construire/matcher about une fois par diagnostic demandé puis le réutiliser pour les actions ; convertir data une seule fois pour la recherche.

**Invariants à préserver :** Équivalence JSON integer/decimal actuelle, attribution des diagnostics et garde buildRevision des edit rows.

**Validation future — non exécutée :** Vérifier données imbriquées, client sans data, actions custom et plusieurs fixes ; compter conversions.

**Preuves dans la révision auditée :**

- [src/lsp/cursiv/actions/DefaultCodeActionProvider.cpp:175](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/lsp/cursiv/actions/DefaultCodeActionProvider.cpp#L175) — const auto offer =
- [src/lsp/cursiv/actions/DefaultCodeActionProvider.cpp:197](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/lsp/cursiv/actions/DefaultCodeActionProvider.cpp#L197) — for (const JsonValue& row : rows->array()) offer

<a id="finding-m-mcp-argument-copy"></a>
### M-MCP-ARGUMENT-COPY — tools/call duplique le JSON des arguments avant un appel en lecture

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **MCP-001 — Décodage et dispatch JSON-RPC**. Périmètres associés : MCP-001, MCP-006, MCP-018.

**Frontière :** Message JSON ou texte reçu → réponse MCP, erreur ou absence de réponse

**Observation :** La Value arguments reçoit *a par copie profonde alors que entry.execute prend const Value& et handle conserve la request pendant l’exécution.

**Condition :** Payload text important pour validate ou outil custom avec grands arrays/objets.

**Coût ou dette :** O(taille JSON) mémoire et copie supplémentaire par appel ; le transport a déjà parsé une version owning.

**Usages et visibilité :** API McpToolFunction synchrone ; garder arguments au-delà de l’appel serait déjà invalide aujourd’hui.

**Changement proposé :** Passer une référence au membre arguments de la request, avec une Value vide locale pour le cas absent.

**Invariants à préserver :** Même valeur null pour argument absent, durée jusqu’au retour de callback, exceptions de tool traduites comme aujourd’hui.

**Validation future — non exécutée :** Vérifier absent/null/objet et outils imbriqués, comparer outputs ; compter allocations sur gros texte.

**Preuves dans la révision auditée :**

- [src/mcp/cursiv/McpServer.cpp:178](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/mcp/cursiv/McpServer.cpp#L178) — json::Value arguments;
- [src/mcp/cursiv/McpServer.cpp:185](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/mcp/cursiv/McpServer.cpp#L185) — outcome = entry.execute(arguments, token);

<a id="finding-m-mcp-scratch-uri-cardinality"></a>
### M-MCP-SCRATCH-URI-CARDINALITY — La réutilisation de slots ne borne pas les identités de scratch

**optimisation · P2 · candidat · confiance moyenne.** Périmètre principal : **MCP-005 — Identités et réservations temporaires**. Périmètres associés : MCP-005.

**Frontière :** Arguments de validation et réservation d’un slot → URI temporaire exclusive jusqu’à la libération

**Observation :** La clé URI est (slot, basename) et non slot seule ; les IDs du store restent après discard, pour chaque nom distinct.

**Condition :** Longue session validate avec noms de fichiers variés et concurrence faible.

**Coût ou dette :** O(nombre d’URI scratch distinctes) métadonnées store/trie et possibles partitions de caches ; all les rescane.

**Usages et visibilité :** Basename participe à routage extension/fileName et peut être lu par providers custom ; ne pas changer silencieusement le nom logique.

**Changement proposé :** Candidat : séparer identité interne stable et nom logique, ou pool explicite par classe de routage avec politique de rétention ; corriger le commentaire de borne immédiatement.

**Invariants à préserver :** IDs publics jamais réutilisés arbitrairement ; aucune suppression de row tant que références/caches peuvent la nommer.

**Validation future — non exécutée :** Vérifier fileNames spéciaux, même extension noms différents, concurrence plusieurs services et providers dépendant URI ; mesurer churn.

**Preuves dans la révision auditée :**

- [src/mcp/cursiv/DefaultMcpService.cpp:230](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/mcp/cursiv/DefaultMcpService.cpp#L230) — return path_to_file_uri("/.mcp-scratch/"
- [src/core/cursiv/workspace/DefaultDocuments.cpp:41](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/workspace/DefaultDocuments.cpp#L41) — _rows.emplace_back(canonical, nullptr);

<a id="finding-m-mcp-validate-double-parse"></a>
### M-MCP-VALIDATE-DOUBLE-PARSE — validate parse deux fois le même scratch dans le chemin par défaut

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **MCP-006 — Validation d’un texte éphémère**. Périmètres associés : MCP-006.

**Frontière :** Texte et langue fournis à validate → diagnostics puis retrait du document créé

**Observation :** Le premier parse de fromTextDocument est détruit par applyUpdate, puis factory.update parse le même texte. Le scratch n’a pas été marqué open.

**Condition :** Tout validate default accepté avec scratch neuf et isOpen=false.

**Coût ou dette :** Deux parses/allocations AST-CST pour un seul texte, le premier entièrement jeté ; coût non mesuré.

**Usages et visibilité :** La factory publique peut être personnalisée ; le constat porte le chemin default et la séquence actuelle de l’outil. DocumentFactory::createDocument est un hook protected (DocumentFactory.hpp:65-72), avec DefaultDocumentBuilder comme ami ; aucun appel direct MCP à ce hook n’est disponible.

**Changement proposé :** Prévoir un point d’entrée de factory pour admettre un document non parsé avec son texte et sa langue, puis laisser le build effectuer le parse une seule fois. Cette évolution d’interface reste à concevoir : createDocument est actuellement protected et accessible au builder ami, pas au service MCP ; conserver ce hook pour les factories spécialisées.

**Invariants à préserver :** Durable install/discard, langue choisie par arguments, publication des rungs, absence de sauvegarde et hooks createDocument ; ne pas marquer artificiellement un buffer ouvert pour éviter le reset.

**Validation future — non exécutée :** Vérifier erreurs de parse, annulation, services custom et symboles cross-document ; compter les appels parser lors de correction.

**Preuves dans la révision auditée :**

- [src/core/cursiv/workspace/DefaultDocumentFactory.cpp:58](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/workspace/DefaultDocumentFactory.cpp#L58) — parse(*document, services, cancelToken);
- [src/core/cursiv/workspace/DefaultDocumentFactory.cpp:111](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/workspace/DefaultDocumentFactory.cpp#L111) — parse(document, services, cancelToken); — second parse par factory.update appelée à la phase Parsed (DefaultDocumentBuilder.cpp:359-362), après admission et builder.update du scratch (DefaultMcpService.cpp:470-477).
- [src/core/cursiv/workspace/DefaultDocumentBuilder.cpp:490](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/workspace/DefaultDocumentBuilder.cpp#L490) — resetDocument(*d, DocumentState::Changed); — applyUpdate réinitialise le scratch fermé ; la garde isOpen && Parsed se trouve à la ligne 489.

<a id="finding-m-mcp-result-copy"></a>
### M-MCP-RESULT-COPY — Plusieurs outils recopient le texte final dans leur résultat

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **MCP-007 — Lecture des diagnostics**. Périmètres associés : MCP-006, MCP-007, MCP-008, MCP-009, MCP-016.

**Frontière :** Filtre de répertoire → diagnostics des documents après le prérequis Validated

**Observation :** Le retour out.empty()?literal:out construit une nouvelle string depuis out lvalue pour le cas non vide. Même schéma dans diagnostics, documents, symbols, languages et résultat validate.

**Condition :** Réponses textuelles longues, notamment diagnostics de workspace.

**Coût ou dette :** Une copie/allocation O(taille réponse) après construction complète de out.

**Usages et visibilité :** McpToolResult owns string ; les callbacks ont fini d’utiliser out lors du retour.

**Changement proposé :** Affecter le fallback si vide puis déplacer out dans McpToolResult, ou choisir explicitement le bras moved ; conserver libellés exacts.

**Invariants à préserver :** Ne pas déplacer avant l’inspection empty ou avant tout append ; même isError et même texte.

**Validation future — non exécutée :** Comparer sorties vides/non vides de chaque outil et mesurer allocations de grosses réponses.

**Preuves dans la révision auditée :**

- [src/mcp/cursiv/DefaultMcpService.cpp:417](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/mcp/cursiv/DefaultMcpService.cpp#L417) — return McpToolResult{out.empty() ? "No diagnostics found." : out};
- [src/mcp/cursiv/DefaultMcpService.cpp:579](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/mcp/cursiv/DefaultMcpService.cpp#L579) — return McpToolResult{out.empty() ? "No symbols found." : out};
- [src/mcp/cursiv/DefaultMcpService.cpp:497](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/mcp/cursiv/DefaultMcpService.cpp#L497) — result = {out.empty() ? "No diagnostics: the text is valid." : out};

<a id="finding-m-protocol-reference-collect"></a>
### M-PROTOCOL-REFERENCE-COLLECT — Deux collectes multi-cibles maintiennent leurs propres règles de dédup

**factorisation · P2 · établi · confiance forte.** Périmètre principal : **MCP-011 — Références d’un nom**. Périmètres associés : MCP-011, MCP-012.

**Frontière :** Position → cibles dénotées et occurrences dans le workspace

**Observation :** LSP trie/déduplique par (sourceDocument,begin,end), MCP references concatène, MCP rename tient encore son seen local par (document,begin).

**Condition :** Nom à plusieurs cibles et évolutions des références/overloads.

**Coût ou dette :** Copies/findReferences répétés et maintenance divergente des identités ; sortie MCP références peut répéter des lignes.

**Usages et visibilité :** La partie collect_references_per_declaration ne dépend que du core, mais son fichier LSP contient aussi une aide LocationLink.

**Changement proposé :** Extraire au core une collecte multi-cibles paramétrée par options et identité de source, puis adapter chaque protocole ; choisir explicitement la politique MCP pour spans de même début.

**Invariants à préserver :** Conserver toutes les cibles, includeDeclaration, ordre document/range et différence entre identité déclaration et identité édition ; ne pas déplacer les types LSP dans core.

**Validation future — non exécutée :** Vérifier multi-références, deux nodes même CST, ranges même début/différentes fins, cibles de langues différentes.

**Preuves dans la révision auditée :**

- [src/lsp/cursiv/navigation/NavigationProviderSupport.cpp:66](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/lsp/cursiv/navigation/NavigationProviderSupport.cpp#L66) — a.sourceEnd == b.sourceEnd;
- [src/mcp/cursiv/DefaultMcpService.cpp:746](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/mcp/cursiv/DefaultMcpService.cpp#L746) — std::vector<std::pair<DocumentId, TextOffset>> seen;

<a id="finding-m-mcp-rename-seen"></a>
### M-MCP-RENAME-SEEN — La dédup des occurrences de renommage MCP est quadratique

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **MCP-012 — Proposition de renommage**. Périmètres associés : MCP-012.

**Frontière :** Position et nouveau nom → liste textuelle des plages à remplacer

**Observation :** Chaque nouvelle occurrence recherche son couple (document,begin) dans tous ceux déjà retenus.

**Condition :** Renommage d’un nom utilisé R fois, surtout grands workspaces.

**Coût ou dette :** O(R²) comparaisons dans le cas d’occurrences distinctes, au-delà du coût findReferences.

**Usages et visibilité :** Seul outil MCP rename ; le linker et provider de descriptions ont déjà leurs hybridations propres, avec identités différentes.

**Changement proposé :** Utiliser set de cette clé conservant ordre d’émission, ou la collecte core triée après décision sur l’identité complète des spans.

**Invariants à préserver :** Ne pas changer silencieusement la clé de dédup, ni mêler naming refusals à cette optimisation ; texte ordre et fin des ranges.

**Validation future — non exécutée :** Comparer duplicats et homonymes multi-target, ranges partagées, puis gros R ; aucun benchmark lancé ici.

**Preuves dans la révision auditée :**

- [src/mcp/cursiv/DefaultMcpService.cpp:746](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/mcp/cursiv/DefaultMcpService.cpp#L746) — std::vector<std::pair<DocumentId, TextOffset>> seen;
- [src/mcp/cursiv/DefaultMcpService.cpp:752](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/mcp/cursiv/DefaultMcpService.cpp#L752) — seen.emplace_back(home.id, begin);

<a id="finding-m-protocol-completion-site"></a>
### M-PROTOCOL-COMPLETION-SITE — La construction du site de référence est recopiée dans MCP

**factorisation · P2 · établi · confiance forte.** Périmètre principal : **MCP-015 — Candidats d’une référence attendue**. Périmètres associés : LSP-024, MCP-015.

**Frontière :** Features au curseur → ReferenceInfo puis entrées de portée textuelles

**Observation :** Deux algorithmes résolvent property/type puis construisent ReferenceInfo, mais seul LSP traite explicitement next.type, entered, propriétaire synthétique et propriété.

**Condition :** Nouvelle feature de complétion, narrowing ou scopeProvider custom lisant le contexte complet.

**Coût ou dette :** Dette de maintenance et résultats divergents, plus recherches de métadonnées recodées ; pas d’accélération chiffrée.

**Usages et visibilité :** ReferenceInfo/NextFeature sont core ; CompletionContext et items restent LSP.

**Changement proposé :** Extraire une fonction core de construction de site recevant document, propriétaire et NextFeature, avec ownership du nœud synthétique rendu explicite.

**Invariants à préserver :** Durée du synthetic node durant visite, narrow, property, type annoncé prioritaire ; MCP doit décider de l’agrégation de plusieurs sites au lieu d’un break implicite.

**Validation future — non exécutée :** Comparer règles nouvellement entrées, property homonyme non-référence, types unanimes/ambigus et providers lisant property.

**Preuves dans la révision auditée :**

- [src/lsp/cursiv/completion/DefaultCompletionProvider.cpp:409](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/lsp/cursiv/completion/DefaultCompletionProvider.cpp#L409) — info.property = feature;
- [src/lsp/cursiv/completion/DefaultCompletionProvider.cpp:397](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/lsp/cursiv/completion/DefaultCompletionProvider.cpp#L397) — next.entered || !reflection.isSubtype
- [src/mcp/cursiv/DefaultMcpService.cpp:1067](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/mcp/cursiv/DefaultMcpService.cpp#L1067) — context.narrow = next.narrow;

<a id="finding-m-cgr-written-pending"></a>
### M-CGR-WRITTEN-PENDING — Dédupliquer les réveils et les dépendances de WrittenReads

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **CGR-002 — Absence de lecture dans la grammaire écrite**. Périmètres associés : CGR-002, CGR-003.

**Frontière :** Expression résolue et dépendances de règles → réponse WrittenReads::nothing stabilisée.

**Observation :** slot évite seulement de répéter _solving si c’est déjà le dernier lecteur du slot. settle insère tous les lecteurs quand les faits changent et ne dispose pas d’un marqueur queued ; des réveils concurrents peuvent recalculer plusieurs fois un même corps avant tout nouveau changement.

**Condition :** Graphe de règles à nombreuses dépendances communes, lazy-ready et résolutions successives.

**Coût ou dette :** Visites redondantes de corps complets et stockage d’arêtes lecteurs répétées ; pas de borne quadratique universelle affirmée puisque les propriétés booléennes limitent les changements.

**Usages et visibilité :** Recherche textuelle dans src, tests, bench, examples, templates, tools et cmake. WrittenReads: 6 fichiers, dont src/engines/cursiv/shared/gen/TwinBranch.cpp, src/engines/cursiv/shared/gen/WrittenReads.cpp, src/engines/cursiv/shared/gen/WarningFound.hpp, src/engines/cursiv/shared/gen/WrittenReads.hpp, src/engines/cursiv/shared/gen/Lowerer.cpp ; nothing_of: 2 fichiers, dont src/engines/cursiv/shared/gen/WrittenReads.cpp, src/engines/cursiv/shared/gen/WrittenReads.hpp. Les instanciations et appels émis comme chaînes restent inclus dans cette lecture ; ce décompte ne prouve pas une utilisation externe.

**Changement proposé :** Ajouter un bit queued par slot et une déduplication des dépendances (ou construire les lecteurs distincts une fois), en gardant découverte lazy et réallocation possibles de l’image.

**Invariants à préserver :** nothing et run sont résolus ensemble ; ne garder aucune référence Slot à travers ready qui peut agrandir les tableaux.

**Validation future — non exécutée :** Après correction, comparer tous warnings et compter solve/body visits pour des diamants partagés ; conserver résultats du fixpoint.

**Preuves dans la révision auditée :**

- [src/engines/cursiv/shared/gen/WrittenReads.cpp:19](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/shared/gen/WrittenReads.cpp#L19) — seul doublon adjacent supprimé
- [src/engines/cursiv/shared/gen/WrittenReads.cpp:121](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/shared/gen/WrittenReads.cpp#L121) — insert de tous lecteurs

<a id="finding-m-cgr-branch-keys"></a>
### M-CGR-BRANCH-KEYS — Dédupliquer les clés des branches sans recherche linéaire répétée

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **CGR-005 — Identité canonique des formes lues**. Périmètres associés : CGR-005.

**Frontière :** Expression et corps expansibles → clé de forme et liste aplatie de positions lues.

**Observation :** branch_keys appelle ranges::find(out,key_) pour chaque branche, bien que key_ soit une identité canonique entière.

**Condition :** Choix de b branches distinctes ou répétitions de clés tardives.

**Coût ou dette :** O(b²) comparaisons dans le cas tout distinct ; key lui-même est déjà mémorisé.

**Usages et visibilité :** Recherche textuelle dans src, tests, bench, examples, templates, tools et cmake. branch_keys: 2 fichiers, dont src/engines/cursiv/shared/gen/TwinBranch.hpp, src/engines/cursiv/shared/gen/TwinBranch.cpp ; TwinReader: 5 fichiers, dont src/engines/cursiv/shared/gen/Lowerer.hpp, src/engines/cursiv/shared/gen/TwinBranch.hpp, src/engines/cursiv/shared/gen/TwinBranch.cpp, tests/engines/shared/AstGeneration_test.cpp, tests/engines/shared/NonConsuming_test.cpp. Les instanciations et appels émis comme chaînes restent inclus dans cette lecture ; ce décompte ne prouve pas une utilisation externe.

**Changement proposé :** Garder out pour l’ordre écrit et un set/marks des clés vues ; scope local à un appel pour ne pas invalider la croissance des clés.

**Invariants à préserver :** Conserver la première apparition et l’ordre des alternatives, ne pas trier out car la préférence est écrite.

**Validation future — non exécutée :** Comparer les réécritures et avertissements exacts ; compter membership selon largeur b.

**Preuves dans la révision auditée :**

- [src/engines/cursiv/shared/gen/TwinBranch.cpp:135](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/shared/gen/TwinBranch.cpp#L135) — find dans out

<a id="finding-m-cgr-option-pairs"></a>
### M-CGR-OPTION-PAIRS — Éviter les comparaisons répétées des mêmes opérandes optionnels

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **CGR-007 — Réécriture d’une suite d’éléments optionnels équivalents**. Périmètres associés : CGR-007.

**Frontière :** Run d’éléments optionnels contenant des lectures équivalentes → choix fondé sur le premier élément présent.

**Observation :** twinned et la collecte de TwinFound parcourent les paires d’options ; alike utilise une clé canonique mais refait flatten/reads dès que les clés correspondent.

**Condition :** Longue suite d’options, avec ou sans doublons.

**Coût ou dette :** O(n²) comparaisons de clés ; en présence de clés égales, reconstructions de vecteurs items. first_present ajoute ses propres copies de suffixes, à traiter séparément.

**Usages et visibilité :** Recherche textuelle dans src, tests, bench, examples, templates, tools et cmake. drop_sequence: 2 fichiers, dont src/engines/cursiv/shared/gen/TwinBranch.hpp, src/engines/cursiv/shared/gen/TwinBranch.cpp ; first_present: 2 fichiers, dont src/engines/cursiv/shared/gen/TwinBranch.hpp, src/engines/cursiv/shared/gen/TwinBranch.cpp. Les instanciations et appels émis comme chaînes restent inclus dans cette lecture ; ce décompte ne prouve pas une utilisation externe.

**Changement proposé :** Pré-calculer pour chaque option clé/eligible=notFold&&reads et indexer la première occurrence et l’existence d’une occurrence ultérieure ; produire les mêmes TwinFound dans l’ordre actuel.

**Invariants à préserver :** Ne pas identifier les options par effets AST ; ne pas réécrire celles qui ne lisent rien ou portent un fold ; garder kRunDepth.

**Validation future — non exécutée :** Comparer warnings, réécritures et effets AST après correction ; mesurer séparément comparaisons et taille de la nouvelle image.

**Preuves dans la révision auditée :**

- [src/engines/cursiv/shared/gen/TwinBranch.cpp:296](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/shared/gen/TwinBranch.cpp#L296) — flatten répété
- [src/engines/cursiv/shared/gen/TwinBranch.cpp:404](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/shared/gen/TwinBranch.cpp#L404) — twinned par paires
- [src/engines/cursiv/shared/gen/TwinBranch.cpp:414](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/shared/gen/TwinBranch.cpp#L414) — collecte par paires

<a id="finding-m-cgr-spelling-prefix"></a>
### M-CGR-SPELLING-PREFIX — Limiter les copies des préfixes lors de l’énumération des opérateurs

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **CGR-008 — Énumération bornée des spellings d’opérateurs**. Périmètres associés : CGR-008.

**Frontière :** Expression d’opérateur → spellings de tokens comparables ou résultat non énumérable.

**Observation :** spell_seq recopie chaque Spelling accumulée dans next puis lui ajoute la nouvelle partie. Même avec une seule alternative, une séquence de L tokens recopie ses préfixes successifs.

**Condition :** Opérateur long ou aliases dont les corps forment de longues séquences ; kSpellings ne borne que le nombre de résultats.

**Coût ou dette :** O(L²) copies de Token dans le cas d’une unique spelling assemblée item par item ; produit des alternatives borné à 64.

**Usages et visibilité :** Recherche textuelle dans src, tests, bench, examples, templates, tools et cmake. Speller: 1 fichiers, dont src/engines/cursiv/shared/gen/UnaryLevelFault.cpp ; spell_seq: 1 fichiers, dont src/engines/cursiv/shared/gen/UnaryLevelFault.cpp. Les instanciations et appels émis comme chaînes restent inclus dans cette lecture ; ce décompte ne prouve pas une utilisation externe.

**Changement proposé :** Préférer un builder qui étend in-place lorsque part ne contient qu’une alternative ; pour les embranchements, partager les préfixes ou pré-allouer la taille finale si connue.

**Invariants à préserver :** Conserver l’ordre des spellings, l’identité terminale, casse folded, refus conservateur sur dépassement et cycles.

**Validation future — non exécutée :** Comparer diagnostics et refus sur opérateurs mixtes littéraux/data ; attribuer copies de Token, sans mesure déjà exécutée.

**Preuves dans la révision auditée :**

- [src/engines/cursiv/shared/gen/UnaryLevelFault.cpp:72](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/shared/gen/UnaryLevelFault.cpp#L72) — copie next de chaque préfixe

<a id="finding-m-cgr-self-bound-readers"></a>
### M-CGR-SELF-BOUND-READERS — Remplacer la liste des lecteurs d’une séquence par un résumé de ses extrémités

**simplification · P3 · établi · confiance forte.** Périmètre principal : **CGR-010 — Repérage des alternatives bornées par leur propre règle**. Périmètres associés : CGR-010.

**Frontière :** Bras résolu d’une règle → verdict sur l’appel de cette règle aux deux extrémités lues.

**Observation :** readers_of alloue et remplit un vector, alors que tous ses consommateurs locaux consultent empty/front/back/size>=2.

**Condition :** Chaque séquence inspectée pour un appel à sa propre règle à l’entrée et à la sortie.

**Coût ou dette :** Allocation et O(n) stockage temporaire évitables ; le parcours O(n) de classification reste nécessaire.

**Usages et visibilité :** Recherche textuelle dans src, tests, bench, examples, templates, tools et cmake. self_bounded_arms: 4 fichiers, dont src/engines/cursiv/shared/gen/gen_report.cpp, src/engines/cursiv/shared/gen/SelfBoundedArm.cpp, src/engines/cursiv/shared/gen/SelfBoundedArm.hpp, tests/engines/shared/NonConsuming_test.cpp ; readers_of: 1 fichiers, dont src/engines/cursiv/shared/gen/SelfBoundedArm.cpp. Les instanciations et appels émis comme chaînes restent inclus dans cette lecture ; ce décompte ne prouve pas une utilisation externe.

**Changement proposé :** Renvoyer first,last,count borné à 2, ou des deux IDs et un bool multiple ; garder la recherche dans les séquences imbriquées.

**Invariants à préserver :** Filtrer les mêmes is_condition_node et préserver les wrappers over_operand ; helper privé, pas d’API publique à supprimer.

**Validation future — non exécutée :** Revue des appels locaux et comparaison des SelfBoundedArm après modification.

**Preuves dans la révision auditée :**

- [src/engines/cursiv/shared/gen/SelfBoundedArm.cpp:53](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/shared/gen/SelfBoundedArm.cpp#L53) — consommateur extrémité
- [src/engines/cursiv/shared/gen/SelfBoundedArm.cpp:60](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/shared/gen/SelfBoundedArm.cpp#L60) — allocation readers

<a id="finding-m-cgr-unbounded-spell"></a>
### M-CGR-UNBOUNDED-SPELL — Calculer une fois les spellings utilisées pour dédupliquer les runs non bornés

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **CGR-014 — Classification des répétitions non bornées dans les conditions**. Périmètres associés : CGR-014.

**Frontière :** Feuilles de condition lisant des tokens → runs sans maximum classés selon la conservation de leurs tours.

**Observation :** none_of appelle spell pour le candidat et chaque antécédent partageant rule/kept ; ces chaînes sont reconstruites lors de chaque nouvelle comparaison.

**Condition :** Nombreux runs de conditions rattachés à une même règle, formes profondes ou partagées.

**Coût ou dette :** O(U²·S) travail de rendu dans le cas de U alertes et coût S de spelling ; allocations temporaires répétées.

**Usages et visibilité :** Recherche textuelle dans src, tests, bench, examples, templates, tools et cmake. find_unbounded_runs: 3 fichiers, dont src/engines/cursiv/shared/gen/UnboundedCondition.hpp, src/engines/cursiv/shared/gen/LowererGap.cpp, src/engines/cursiv/shared/gen/UnboundedCondition.cpp ; unbounded_condition_text: 5 fichiers, dont src/engines/cursiv/shared/gen/UnboundedCondition.hpp, src/engines/cursiv/shared/gen/gen_report.hpp, src/engines/cursiv/shared/gen/UnboundedCondition.cpp, tests/engines/lr/LrLowered_cases.cpp, tests/engines/ll/LlConditions_test.cpp. Les instanciations et appels émis comme chaînes restent inclus dans cette lecture ; ce décompte ne prouve pas une utilisation externe.

**Changement proposé :** Calculer le spelling du candidat une fois, conserver un index local par (rule,kept,spelling), et mémoïser le rendu par node si l’image reste immuable.

**Invariants à préserver :** Le rendu affiche parfois ... pour des formes non prises en charge : conserver l’identité actuelle du diagnostic lors d’une optimisation pure, ou traiter un changement sémantique séparément.

**Validation future — non exécutée :** Comparer ordre/nombre/texte des warnings ; compter appels spell et allocations.

**Preuves dans la révision auditée :**

- [src/engines/cursiv/shared/gen/UnboundedCondition.cpp:131](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/shared/gen/UnboundedCondition.cpp#L131) — déduplication avec spell répété
- [src/engines/cursiv/shared/gen/UnboundedCondition.cpp:84](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/shared/gen/UnboundedCondition.cpp#L84) — rendu

<a id="finding-m-com-election-hash"></a>
### M-COM-ELECTION-HASH — L’élection recalcule les empreintes des mêmes histoires pendant le regroupement

**optimisation · P3 · établi · confiance forte.** Périmètre principal : **COM-006 — Élection commune entre groupes de buts**. Périmètres associés : LR-059, COM-006.

**Frontière :** Goals vivants et callbacks Fold/Same/Kind/race → but élu et alternatives de racine compatibles.

**Observation :** Le hasher de groups appelle Fold sur la chaîne complète à chaque demande du unordered_map ; le calcul n’est pas stocké par lien pendant cette élection. Les fonctions LL/LR de fold remontent parent.

**Condition :** Plusieurs buts, histoires longues et réhachages/collisions du regroupement.

**Coût ou dette :** Chaque invocation de hash coûte O(H) pour H liens, en plus de l’égalité exacte ; le nombre de recomputations dépend du conteneur.

**Usages et visibilité :** repair_elect est utilisé par les deux réparateurs ; les liens sont stabilisés pendant ce regroupement.

**Changement proposé :** Calculer une empreinte par lien rencontré pour la durée de l’élection, ou stocker la clé et son empreinte dans une entrée locale ; réserver groups au nombre de buts.

**Invariants à préserver :** Garder Same comme preuve exacte, l’ordre d’apparition des groupes, les racines et les choix ex aequo. Ne pas réutiliser un cache après réorientation de liens.

**Validation future — non exécutée :** Compter les visites parent et allocations sur de nombreux buts à préfixe partagé ; comparer le but élu et also.

**Preuves dans la révision auditée :**

- [src/engines/cursiv/shared/runtime/RepairSearch.hpp:179](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/shared/runtime/RepairSearch.hpp#L179) — Empreinte recalculée par le hasher
- [src/engines/cursiv/lr/runtime/lr_search.hpp:894](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/runtime/lr_search.hpp#L894) — Implémentation LR parcourant les liens

<a id="finding-m-com-tied-prefix"></a>
### M-COM-TIED-PREFIX — Chaque fusion d’insertions réinspecte le préfixe commun à la recherche d’un geste

**optimisation · P2 · candidat · confiance moyenne.** Périmètre principal : **COM-008 — Conservation des insertions alternatives après fusion**. Périmètres associés : COM-008.

**Frontière :** Chemins à égalité et insertion initiale → alternatives rattachées au chemin conservé puis nommées une fois.

**Observation :** TiedInserts::tie remonte de p.shared à la racine jusqu’au premier geste, même si GestureRank vient de trouver une divergence courte ; plusieurs fusions sous un long préfixe sans geste répètent cette lecture.

**Condition :** M fusions partageant H liens de bras sans geste.

**Coût ou dette :** O(M×H) visites du préfixe dans ce scénario ; le coût est compté dans walked mais la répétition subsiste.

**Usages et visibilité :** tie est appelé depuis repair_meet pour LL/LR ; les liens peuvent être remplacés lors d’un handoff.

**Changement proposé :** Étudier une information de premier geste sur préfixe ou un cache local invalidé par handed ; conserver la marche actuelle tant qu’une invalidation exacte n’est pas établie.

**Invariants à préserver :** Les insertions sous les chemins abandonnés et l’effet des liens réorientés doivent rester identiques.

**Validation future — non exécutée :** Construire ultérieurement des fusions à long préfixe de bras, instrumenter walked et comparer also avant/après.

**Preuves dans la révision auditée :**

- [src/engines/cursiv/shared/runtime/TiedInserts.hpp:42](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/shared/runtime/TiedInserts.hpp#L42) — Remontée répétée du préfixe
- [src/engines/cursiv/shared/runtime/RepairSearch.hpp:155](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/shared/runtime/RepairSearch.hpp#L155) — Mutation des liens à prendre en compte

<a id="finding-m-com-typo-ranked-temp"></a>
### M-COM-TYPO-RANKED-TEMP — Le classement des typos conserve trois tableaux de candidats et d’indices

**optimisation · P3 · établi · confiance forte.** Périmètre principal : **COM-014 — Identité et ordre des candidats typo**. Périmètres associés : LR-056, COM-014.

**Frontière :** Flux de Typo pouvant se répéter → liste unique classée par coût, fin et ordre d’émission.

**Observation :** ranked() const crée un vector d’indices, le trie puis copie _found dans un nouveau vector ; les appels de réparation consomment le résultat d’un collecteur local qui est ensuite détruit.

**Condition :** N candidats distincts dans un rescan.

**Coût ou dette :** O(N log N) tri nécessaire, mais O(N) indices et O(N) copies de Typo supplémentaires pendant que _found et _emitted vivent encore.

**Usages et visibilité :** ranked appelé dans ll_search_typos et lr_rescan_candidates ; les tests vérifient aussi le classement.

**Changement proposé :** Ajouter une voie de consommation explicite qui trie/permutte puis transfère _found, ou rapprocher Typo et son horloge dans une entrée sortable ; garder une variante const si nécessaire.

**Invariants à préserver :** Coût croissant, fin décroissante, puis horloge de la meilleure émission ; l’amélioration d’un candidat change son horloge.

**Validation future — non exécutée :** Comparer tous les ordres et mises à jour de coût des tests existants ; instrumenter allocations et pic mémoire.

**Preuves dans la révision auditée :**

- [src/engines/cursiv/shared/runtime/TypoCandidates.hpp:37](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/shared/runtime/TypoCandidates.hpp#L37) — Classement non consommant
- [src/engines/cursiv/shared/runtime/TypoCandidates.hpp:38](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/shared/runtime/TypoCandidates.hpp#L38) — Indices temporaires
- [src/engines/cursiv/shared/runtime/TypoCandidates.hpp:49](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/shared/runtime/TypoCandidates.hpp#L49) — Copie du résultat

<a id="finding-m-com-flatmap-reset"></a>
### M-COM-FLATMAP-RESET — Le reset des FlatMap réutilisées impose une nouvelle série de croissances

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **COM-015 — Table de clés entières à insertion seule**. Périmètres associés : LR-017, LR-056, LR-057, LR-058, LR-062, COM-011, COM-015.

**Frontière :** Clés uint64_t et valeurs FlatMap → insertion/recherche exactes à travers croissance et reset.

**Observation :** reset réassigne une taille de 64 cases ; grow calcule sa prochaine taille depuis size et alloue deux nouveaux vectors. Après un grand remplissage, la capacité retenue par assign n’est donc pas utilisée par les croissances suivantes.

**Condition :** Marches successives dont seen ou un autre mémo dépasse 32 entrées après reset.

**Coût ou dette :** Rehachages et allocations géométriques répétés à chaque cycle ; les grandes capacités retenues peuvent coexister avec les nouveaux buffers pendant grow.

**Usages et visibilité :** FlatMap utilisée par TypoScratch::seen, LexRelayRun::_memo et des mémoïsations de génération LR ; reset est actif.

**Changement proposé :** Distinguer clear conservant le nombre de buckets et réduction explicite de capacité ; éventuellement vider les seules cases touchées, avec une politique de rétention bornée.

**Invariants à préserver :** Clé sentinelle ~0 interdite, valeurs réinitialisées à V{}, références invalidées à insertion ; éviter une rétention illimitée après entrée exceptionnelle.

**Validation future — non exécutée :** Mesurer allocations sur plusieurs cycles identiques et un grand cycle suivi de petits ; comparer tous les résultats de recherche.

**Preuves dans la révision auditée :**

- [src/engines/cursiv/shared/table/FlatMap.hpp:28](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/shared/table/FlatMap.hpp#L28) — Taille réinitialisée à 64
- [src/engines/cursiv/shared/table/FlatMap.hpp:57](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/shared/table/FlatMap.hpp#L57) — Nouveaux buffers dimensionnés par size
- [src/engines/cursiv/shared/runtime/TypoWalk.hpp:174](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/shared/runtime/TypoWalk.hpp#L174) — Usage récurrent

<a id="finding-m-com-flatmap-dup-grow"></a>
### M-COM-FLATMAP-DUP-GROW — L’internement peut croître avant de reconnaître une clé déjà présente

**optimisation · P3 · établi · confiance forte.** Périmètre principal : **COM-015 — Table de clés entières à insertion seule**. Périmètres associés : LR-046, COM-015.

**Frontière :** Clés uint64_t et valeurs FlatMap → insertion/recherche exactes à travers croissance et reset.

**Observation :** emplace teste la charge et appelle grow avant de chercher la clé ; à demi-remplissage une simple réadmission d’une clé existante double la table. Le même ordre apparaît dans LrGssRope::intern : grow précède le probe exact.

**Condition :** Table au seuil de charge et appel emplace sur une clé déjà connue.

**Coût ou dette :** Un rehash O(capacité) et deux buffers supplémentaires pour une opération qui ne modifie pas le contenu.

**Usages et visibilité :** Employé comme test d’admission par TypoWalk et les structures d’internement ; la duplication est un cas normal. LrGssRope est consommé par Forest et Digest ; appliquer le même principe localement sans unifier leurs conteneurs.

**Changement proposé :** Rechercher d’abord si la table est non vide ; ne croître et recalculer la case que pour une insertion nouvelle au seuil.

**Invariants à préserver :** Garder le seuil, le probe exact et le contrat de références ; éviter de dégrader systématiquement les insertions nouvelles par une recherche inutile.

**Validation future — non exécutée :** Comparer compteurs grow sur une table au seuil recevant des doublons puis de nouvelles clés ; vérifier coûts de probe.

**Preuves dans la révision auditée :**

- [src/engines/cursiv/shared/table/FlatMap.hpp:18](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/shared/table/FlatMap.hpp#L18) — Croissance avant identité
- [src/engines/cursiv/shared/table/FlatMap.hpp:20](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/shared/table/FlatMap.hpp#L20) — Identité testée ensuite
- [src/engines/cursiv/lr/runtime/LrGssRope.hpp:335](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/lr/runtime/LrGssRope.hpp#L335) — Même croissance avant recherche du doublon

<a id="finding-m-com-chain-temp"></a>
### M-COM-CHAIN-TEMP — Les chaînes concaténées sont matérialisées avant de savoir si elles existent

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **COM-022 — Composition de chaînes et de leur documentation**. Périmètres associés : LR-008, LR-012, COM-021, COM-022.

**Frontière :** Deux runs ou identifiants de chaînes, ou demande de turn → séquence composée et métadonnées associées.

**Observation :** intern_joined construit un vector depuis a puis insère b avant la recherche d’identité ; turn_chain copie aussi son run avant le probe. Des appels répétés qui retrouvent une chaîne existante paient donc quand même cette matérialisation.

**Condition :** Concaténations répétées lors du lowering/settle, y compris hits du pool.

**Coût ou dette :** O(|a|+|b|) copie temporaire et une ou deux allocations par concat non triviale ; turn copie O(n).

**Usages et visibilité :** concat utilisé par lr_lower_settle, lr_build_guards, LlLowerEmit et ll_build ; turn_chain par LrLowerer.

**Changement proposé :** Introduire un probe exact sur deux spans avec hash incrémental, puis copier seulement sur miss ; pour turn, comparer le run avant toute mutation du pool.

**Invariants à préserver :** Préserver ordre des actions, distinction bare/kNoRow, doc prioritaire et turn ; les spans peuvent référencer acts, donc sécuriser les sources avant sa réallocation sur miss.

**Validation future — non exécutée :** Comparer tables et CST générés ; compter allocations et hits du pool avec nombreux sites partageant des écritures.

**Preuves dans la révision auditée :**

- [src/engines/cursiv/shared/gen/ChainPool.hpp:136](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/shared/gen/ChainPool.hpp#L136) — Concat temporaire
- [src/engines/cursiv/shared/gen/ChainPool.hpp:86](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/shared/gen/ChainPool.hpp#L86) — Copie pour turn
- [src/engines/cursiv/shared/gen/ChainPool.hpp:150](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/engines/cursiv/shared/gen/ChainPool.hpp#L150) — Égalité exacte à conserver

<a id="finding-m-sch-cancel-drain"></a>
### M-SCH-CANCEL-DRAIN — Le drainage continue index par index après annulation

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **SCH-003 — Issue d’un parallelFor interrompu**. Périmètres associés : SCH-003, SCH-005.

**Frontière :** Corps pouvant lever et token → jonction puis exception ou annulation

**Observation :** Le wrapper ne fait plus de travail utile mais WorkerPool::drain continue à appeler job.body pour chaque indice restant.

**Condition :** Annulation ou premier échec sur un grand range, surtout corps bon marché.

**Coût ou dette :** O(N restant) appels indirects et lectures atomiques ; aucun gain temporel mesuré.

**Usages et visibilité :** Corps protégés de TaskScheduler ; ne pas imposer cette politique au WorkerPool public sans token.

**Changement proposé :** Permettre au job du scheduler de signaler un arrêt de réservation de tranches, tout en terminant les parts déjà publiées.

**Invariants à préserver :** Attendre pending==0, conserver la priorité de la première exception sur OperationCancelled et la durée des références de pile.

**Validation future — non exécutée :** Vérifier ultérieurement annulation au milieu d’une tranche, exceptions simultanées et jobs imbriqués ; mesurer latence de sortie.

**Preuves dans la révision auditée :**

- [src/core/cursiv/execution/TaskScheduler.cpp:64](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/execution/TaskScheduler.cpp#L64) — Once cancelled or once a sibling failed
- [src/core/cursiv/execution/WorkerPool.cpp:91](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/execution/WorkerPool.cpp#L91) — void WorkerPool::drain

<a id="finding-m-sch-self-index"></a>
### M-SCH-SELF-INDEX — Identifier le worker sans rescanner tout le pool

**optimisation · P3 · candidat · confiance moyenne.** Périmètre principal : **SCH-006 — Distribution des publications externes**. Périmètres associés : SCH-006.

**Frontière :** Demandeur interne ou externe et parts → queues destinataires

**Observation :** selfIndex compare l’identifiant du thread aux W workers jusqu’au match ; un soumetteur externe paie W comparaisons à chaque run.

**Condition :** Nombreux petits jobs ou pools larges.

**Coût ou dette :** O(W) par identification ; coût secondaire possible face au travail du job.

**Usages et visibilité :** run et onWorkerThread ; plusieurs pools peuvent coexister et un worker peut soumettre à un autre pool.

**Changement proposé :** Candidat : installer dans workerLoop un contexte TLS (pool, index), restauré à la sortie ; garder le fallback externe.

**Invariants à préserver :** Ne pas confondre appartenance au thread et au pool ; destruction/recréation et soumission croisée.

**Validation future — non exécutée :** Vérifier plusieurs pools, nesting croisé et threads externes ; comparer les petits lots.

**Preuves dans la révision auditée :**

- [src/core/cursiv/execution/WorkerPool.cpp:82](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/execution/WorkerPool.cpp#L82) — for (std::size_t i = 0; i < _workers.size(); ++i)
- [src/core/cursiv/execution/WorkerPool.cpp:287](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/execution/WorkerPool.cpp#L287) — const std::size_t self = selfIndex();

<a id="finding-m-sch-corun-sweeps"></a>
### M-SCH-CORUN-SWEEPS — Réexaminer les scans complets dans le tail de corun

**optimisation · P2 · candidat · confiance moyenne.** Périmètre principal : **SCH-009 — Attente coopérative d’un job imbriqué**. Périmètres associés : SCH-009.

**Frontière :** Demandeur attendant pending → exécution d’autres tasks puis jonction

**Observation :** Le publisher balaie W+B victimes à chaque miss, y compris après le seuil où le worker ne fait que yield puis recommence.

**Condition :** Une part longue termine seule, ou un run imbriqué attend ses derniers producteurs.

**Coût ou dette :** O((W+B) × nombre de tentatives) accès aux queues durant l’attente ; durée dépendante de l’ordonnancement.

**Usages et visibilité :** corun seulement ; la boucle ordinaire des workers utilise un notifier avec un autre invariant.

**Changement proposé :** Candidat : espacer les scans complets avec backoff ou notification de progrès, en maintenant une participation périodique.

**Invariants à préserver :** Un worker doit pouvoir exécuter le travail nécessaire au job qu’il attend ; ne pas fusionner corun et parking ordinaire sans preuve.

**Validation future — non exécutée :** Vérifier nesting profond, dépendances croisées, charge déséquilibrée et latence d’un nouveau travail ; mesures différées.

**Preuves dans la révision auditée :**

- [src/core/cursiv/execution/WorkerPool.cpp:255](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/execution/WorkerPool.cpp#L255) — if (++misses < kCorunSpins)
- [src/core/cursiv/execution/WorkerPool.cpp:259](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/execution/WorkerPool.cpp#L259) — if (isWorker) {

<a id="finding-m-sch-queue-baseline"></a>
### M-SCH-QUEUE-BASELINE — Dimensionner la capacité initiale des queues du pool

**optimisation · P3 · candidat · confiance moyenne.** Périmètre principal : **SCH-011 — Croissance et rétention des tableaux de deque**. Périmètres associés : SCH-011.

**Frontière :** Queue pleine → nouveau tableau publié et anciens tableaux conservés

**Observation :** Chaque worker et bucket construit 1024 slots atomiques et un vecteur garbage de capacité 32, même si peu de tâches y sont en file.

**Condition :** Pool large et séries de petits parallelFor ; le pic de nesting reste à déterminer.

**Coût ou dette :** Mémoire initiale proportionnelle à (W+B)×1024 pointeurs, plus stockage des anciens tableaux après croissance.

**Usages et visibilité :** Le constructeur TaskQueue est public ; optimiser le choix du pool plutôt que changer son contrat général.

**Changement proposé :** Candidat : choisir un logSize initial plus petit pour les queues du pool, après mesure des tailles réelles ; laisser la croissance intacte.

**Invariants à préserver :** Ne libérer aucun ancien tableau tant que des voleurs peuvent le lire ; pas de shrink concurrent improvisé.

**Validation future — non exécutée :** Mesurer files et nombre de croissances sur gros/nested jobs ; vérifier échec d’allocation pendant resize.

**Preuves dans la révision auditée :**

- [src/core/cursiv/execution/TaskQueue.hpp:107](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/execution/TaskQueue.hpp#L107) — slots(new std::atomic<T>
- [src/core/cursiv/execution/TaskQueue.hpp:120](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/execution/TaskQueue.hpp#L120) — Array* resize(

<a id="finding-m-sch-spinlock-surface"></a>
### M-SCH-SPINLOCK-SURFACE — Séparer le primitive pause de la classe SpinLock sans consommateur local

**simplification · P3 · candidat · confiance moyenne.** Périmètre principal : **SCH-013 — Verrou court et attente active**. Périmètres associés : SCH-013.

**Frontière :** Tentative lock → acquisition exclusive puis publication à unlock

**Observation :** L’en-tête nommé SpinLock est inclus pour pause_once ; le lock et son backoff n’ont pas d’appel local.

**Condition :** Maintenance de la surface execution et dépendances d’en-têtes.

**Coût ou dette :** Couplage de lecture et surface à maintenir, pas de coût runtime démontré du code non appelé.

**Usages et visibilité :** Classe exposée dans un header de bibliothèque malgré namespace detail ; absence de consommateurs externes non établie.

**Changement proposé :** Extraire la déclaration de pause_once dans un header dédié ; évaluer séparément une dépréciation de SpinLock après contrôle API.

**Invariants à préserver :** Conserver les branches de plateforme et la compatibilité d’inclusion si le header est public.

**Validation future — non exécutée :** Rechercher les consommateurs downstream et vérifier compilation multi-plateforme après une éventuelle évolution.

**Preuves dans la révision auditée :**

- [src/core/cursiv/execution/WorkerPool.cpp:13](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/execution/WorkerPool.cpp#L13) — #include "cursiv/execution/SpinLock.hpp"
- [src/core/cursiv/utils/ConcurrentTable.hpp:17](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/utils/ConcurrentTable.hpp#L17) — #include "cursiv/execution/SpinLock.hpp"

<a id="finding-m-cac-clear-alloc"></a>
### M-CAC-CLEAR-ALLOC — L’invalidation conservant capacité réalloue toute la table

**optimisation · P2 · candidat · confiance moyenne.** Périmètre principal : **CAC-005 — Effacement et générations de table**. Périmètres associés : CAC-005, CAC-013, CAC-014.

**Frontière :** clear et keepCapacity → nouvelle génération vide

**Observation :** Table::make construit counts et groups pour chaque clear ; l’ancienne capacité entière reste parfois en retraite simultanément.

**Condition :** Cache chaud important invalidé à chaque update ou rung, notamment à répétition avant remplissage.

**Coût ou dette :** O(G) initialisation et allocation par invalidation, G groupes, avec recouvrement mémoire des générations.

**Usages et visibilité :** WorkspaceCache et DocumentCache utilisent ces invalidations ; concurrence des lecteurs interdit un memset en place.

**Changement proposé :** Candidat : fast path documenté pour table vide ou politique de capacité retenue plafonnée ; considérer réemploi seulement après quiescence.

**Invariants à préserver :** Une génération doit avancer même si aucune entrée n’est présente afin d’exclure les providers antérieurs ; lecteurs et computations encore en vol.

**Validation future — non exécutée :** Vérifier clear pendant provider et growth, valeurs non triviales, taille après gros pic ; mesurer allocations sur edits.

**Preuves dans la révision auditée :**

- [src/core/cursiv/utils/ConcurrentTable.hpp:233](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/utils/ConcurrentTable.hpp#L233) — for (std::size_t at = 0; at < groups; ++at)
- [src/core/cursiv/utils/SimpleCache.hpp:43](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/utils/SimpleCache.hpp#L43) — _values.clear(true)
- [src/core/cursiv/utils/DocumentCache.hpp:81](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/utils/DocumentCache.hpp#L81) — clear(DocumentId document)

<a id="finding-m-cac-collect-lifecycle"></a>
### M-CAC-COLLECT-LIFECYCLE — La mémoire retirée attend une future activité de collecte

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **CAC-008 — Collecte globale des tables retirées**. Périmètres associés : CAC-002, CAC-007, CAC-008.

**Frontière :** Epochs actives et retraits → lot libérable détruit

**Observation :** Aucun timer ni collecte au départ du dernier lecteur ; les seuils comptent seulement les retraits depuis la collecte précédente.

**Condition :** Fin de build avec moins de 32 retraits ET moins de 8 Mio comptés, ou lecteur long lors du dernier sweep, puis inactivité.

**Coût ou dette :** Rétention différée des tables/Node ; allocated exclut les allocations séparées des valeurs, donc le seuil bytes ne borne pas la mémoire réelle.

**Usages et visibilité :** Helper public detail explicitement appelé dans tests ; TableEpochs est global au processus.

**Changement proposé :** Ajouter un point explicite de collecte à une frontière de repos appropriée ou une demande différée ; distinguer mémoire comptée et possédée.

**Invariants à préserver :** Ne pas collecter au départ de chaque Guard ni bloquer les hits ; free hors verrou, fallback sans membarrier, lecteurs imbriqués.

**Validation future — non exécutée :** Vérifier inactivité après lecteurs longs, coût du sweep par plateforme et destructeurs réentrants ; aucune fuite permanente affirmée.

**Preuves dans la révision auditée :**

- [src/core/cursiv/utils/ConcurrentTable.cpp:100](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/utils/ConcurrentTable.cpp#L100) — void collect_locked
- [src/core/cursiv/utils/ConcurrentTable.hpp:265](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/utils/ConcurrentTable.hpp#L265) — entries kept apart are not counted
- [src/core/cursiv/utils/ConcurrentTable.cpp:101](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/utils/ConcurrentTable.cpp#L101) — s.sinceCollection = 0;

<a id="finding-m-cac-published-reclaim-lock"></a>
### M-CAC-PUBLISHED-RECLAIM-LOCK — Détruire les snapshots libérés hors des verrous de publication

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **CAC-010 — Publication et retrait des snapshots**. Périmètres associés : CAC-010.

**Frontière :** Nouveau shared_ptr ou retire → ancienne valeur libérée après passage des lecteurs

**Observation :** collectLocked efface les Retired et peut déclencher la destruction de gros Exports/Incoming sous _writers, souvent sous le mutex index englobant.

**Condition :** Invalidation/publication après la sortie des lecteurs d’un gros snapshot.

**Coût ou dette :** Temps de libération proportionnel aux conteneurs détruits allonge la section critique ; readers restent sans mutex mais rebuild/writers attendent.

**Usages et visibilité :** Published générique possède des T potentiellement coûteux ; index appelle ce chemin pour chaque changement de contenu/références.

**Changement proposé :** Collecter les owners devenus libérables dans un lot à détruire après libération des locks ; déplacer aussi le point d’appel index hors de son verrou si nécessaire avec génération.

**Invariants à préserver :** Le raw publié doit toujours posséder un owner ; chaque retrait garde son masque owed et aucune destruction anticipée.

**Validation future — non exécutée :** Vérifier réentrance des destructeurs, exception allocation du lot, lecteur à cheval sur publication et charge de gros snapshots.

**Preuves dans la révision auditée :**

- [src/core/cursiv/utils/Published.hpp:116](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/utils/Published.hpp#L116) — void collectLocked() const
- [src/core/cursiv/utils/Published.hpp:131](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/utils/Published.hpp#L131) — std::shared_ptr<const T> value;
- [src/core/cursiv/workspace/DefaultIndexManager.cpp:375](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/workspace/DefaultIndexManager.cpp#L375) — _incoming.publish(std::move(built))

<a id="finding-m-cac-memo-factor"></a>
### M-CAC-MEMO-FACTOR — Mutualiser le petit protocole get-or-compute des caches

**factorisation · P3 · établi · confiance forte.** Périmètre principal : **CAC-011 — Factory de mémoïsation partagée**. Périmètres associés : CAC-011.

**Frontière :** Miss, factory et génération → valeur retournée et éventuellement mémorisée

**Observation :** Les deux classes implémentent la même suite de lookup, génération, calcul hors lock et insertion first-wins.

**Condition :** Toute évolution de la sémantique clear/provider ou du retour par valeur.

**Coût ou dette :** Maintenance doublée ; aucune accélération promise.

**Usages et visibilité :** Templates publics ; seul le noyau privé sur Values est commun, pas l’invalidation.

**Changement proposé :** Extraire un helper interne de mémoïsation qui reçoit la table et le provider ; conserver le bypass kNoDocument dans DocumentCache.

**Invariants à préserver :** Génération capturée avant provider, first-wins, provider non déplacé deux fois, retour de computed si insertion refusée.

**Validation future — non exécutée :** Vérifier valeurs mobiles/non triviales, provider réentrant, concurrence même clé et clear pendant calcul.

**Preuves dans la révision auditée :**

- [src/core/cursiv/utils/SimpleCache.hpp:28](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/utils/SimpleCache.hpp#L28) — const std::uint64_t generation
- [src/core/cursiv/utils/DocumentCache.hpp:65](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/utils/DocumentCache.hpp#L65) — const std::uint64_t generation

<a id="finding-m-cac-document-partitions"></a>
### M-CAC-DOCUMENT-PARTITIONS — Les lectures sans valeur créent des partitions conservées

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **CAC-012 — Partitionnement documentaire et accès TLS**. Périmètres associés : MCP-005, CAC-012.

**Frontière :** DocumentId, clé et instance → partition stable puis lookup

**Observation :** Le get sans provider appelle valuesOf : un lookup négatif d’un nouvel ID alloue Values, l’inscrit dans _documents et le garde jusqu’à destruction du cache.

**Condition :** Beaucoup d’IDs visités puis supprimés, ou sondages négatifs dans une session longue.

**Coût ou dette :** O(H) partitions historiques ; suppression réduit leur capacité mais ne supprime ni objet ni mapping.

**Usages et visibilité :** DocumentCache public ; les pointers TLS/computations motivent la stabilité, pas une absence d’usage.

**Changement proposé :** Pour le get sans provider, rechercher une partition existante sans la créer ; traiter la récupération des partitions supprimées comme projet distinct.

**Invariants à préserver :** Le provider get conserve un Values stable durant compute/insert ; serial TLS interdit réutilisation d’une adresse de cache.

**Validation future — non exécutée :** Vérifier aucun effet de bord attendu du get sondage, concurrence création/clear, et mémoire après churn d’IDs.

**Preuves dans la révision auditée :**

- [src/core/cursiv/utils/DocumentCache.hpp:77](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/utils/DocumentCache.hpp#L77) — return valuesOf(document).find(key);
- [src/core/cursiv/utils/DocumentCache.hpp:156](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/utils/DocumentCache.hpp#L156) — mutable std::deque<Values> _owned;

<a id="finding-m-wsp-factory-canonical"></a>
### M-WSP-FACTORY-CANONICAL — La chaîne de factory répète normalisation et routage

**factorisation · P3 · candidat · confiance moyenne.** Périmètre principal : **WSP-001 — Création d’un document et choix de langage**. Périmètres associés : LSP-011, WSP-001.

**Frontière :** Texte, URI et éventuel languageId → document lié à ses services et parse initial

**Observation :** Le chemin disque effectue normalize_uri dans trois niveaux et servicesFor dans deux, avec des strings intermédiaires.

**Condition :** Création de chaque document par fromUri, notamment au scan initial.

**Coût ou dette :** Travail URI/routage redondant proportionnel à la longueur des URI ; coût de parse souvent dominant.

**Usages et visibilité :** Méthodes virtuelles de factory ; ne pas contourner silencieusement les overrides fromString/fromTextDocument.

**Changement proposé :** Candidat : chemin interne recevant URI canonique et services déjà choisis, tout en précisant quelles surcharges sont les hooks contractuels.

**Invariants à préserver :** LanguageId explicite prioritaire, canonicalisation et ordre d’installation texte/arbre ; compatibilité des dérivées.

**Validation future — non exécutée :** Comparer appels de hooks des factories personnalisées et URI encodées ; mesurer seulement ensuite.

**Preuves dans la révision auditée :**

- [src/core/cursiv/workspace/DefaultDocumentFactory.cpp:78](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/workspace/DefaultDocumentFactory.cpp#L78) — return fromString(shared.fileSystemProvider
- [src/core/cursiv/workspace/DefaultDocumentFactory.cpp:68](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/workspace/DefaultDocumentFactory.cpp#L68) — return fromTextDocument(

<a id="finding-m-wsp-active-documents"></a>
### M-WSP-ACTIVE-DOCUMENTS — all parcourt et réserve les identités historiques

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **WSP-005 — Insertion et récupération dans le store**. Périmètres associés : LSP-020, MCP-005, MCP-007, MCP-008, WSP-005, WSP-006, WSP-009, WSP-010.

**Frontière :** Document ou URI → objet enregistré ou objet déjà présent

**Observation :** Une session ayant H IDs mais A documents actifs crée un vecteur de capacité H, balaie H lignes et copie A shared_ptr à chaque all.

**Condition :** Churn de fichiers/scratchs ou grands workspaces avec plusieurs passes de build.

**Coût ou dette :** O(H) scan/allocation réservée par appel au lieu de dépendre de A ; copies atomiques des shared_ptr actifs.

**Usages et visibilité :** API Documents::all est publique ; les consommateurs produit l’utilisent pour scanners l’état.

**Changement proposé :** Maintenir un index d’IDs actifs, ou fournir un visiteur sous lecture pour les appels internes ; garder les tombstones URI dans le store.

**Invariants à préserver :** Identités stables, ordre de all actuellement par ID, handles propriétaires lorsque l’appel quitte le lock.

**Validation future — non exécutée :** Vérifier suppression/recréation, ordre déterministe et durée des handles ; mesurer mémoire après churn.

**Preuves dans la révision auditée :**

- [src/core/cursiv/workspace/DefaultDocuments.cpp:129](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/workspace/DefaultDocuments.cpp#L129) — for (const Row& r : _rows)
- [src/core/cursiv/workspace/DefaultDocumentBuilder.cpp:293](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/workspace/DefaultDocumentBuilder.cpp#L293) — const auto held = _shared.documents->all();

<a id="finding-m-wsp-build-options-copy"></a>
### M-WSP-BUILD-OPTIONS-COPY — Éviter la copie des catégories pour un test de validation

**optimisation · P3 · établi · confiance forte.** Périmètre principal : **WSP-007 — Conservation d’une demande de build**. Périmètres associés : WSP-007, VAL-009.

**Frontière :** BuildOptions par document → demande conservée, reprise ou marquée complète

**Observation :** pending appelle getBuildOptions qui copie le vecteur de catégories alors que seul validation est lu.

**Condition :** Phase Validated avec beaucoup de documents et catégories explicites.

**Coût ou dette :** Une acquisition de mutex et une éventuelle allocation/copie du vecteur par document pour un booléen.

**Usages et visibilité :** getBuildOptions public/protégé peut rester par valeur ; l’optimisation vise le contrôle interne de pending.

**Changement proposé :** Ajouter un accès interne au flag sous lock, ou figer les options nécessaires dans la liste du run sans garder une référence au map mutable.

**Invariants à préserver :** Les requêtes inachevées survivent aux nouveaux builds ; aucune référence à une valeur effaçable hors du verrou.

**Validation future — non exécutée :** Vérifier reprise de catégories manquantes, reconfiguration et builds sans validation ; compter allocations.

**Preuves dans la révision auditée :**

- [src/core/cursiv/workspace/DefaultDocumentBuilder.cpp:258](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/workspace/DefaultDocumentBuilder.cpp#L258) — return at == _buildOptions.end() ? _updateBuildOptions : at->second.options;
- [src/core/cursiv/workspace/DefaultDocumentBuilder.cpp:66](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/workspace/DefaultDocumentBuilder.cpp#L66) — (phaseEnd != DocumentState::Validated

<a id="finding-m-wsp-document-wake-fanout"></a>
### M-WSP-DOCUMENT-WAKE-FANOUT — Les publications documentaires réveillent tous les documents attendus

**optimisation · P2 · candidat · confiance moyenne.** Périmètre principal : **WSP-011 — Attente de l’état d’un document**. Périmètres associés : WSP-011, WSP-026.

**Frontière :** État attendu et DocumentId → Reached, Cancelled, BuildFailed ou Unreachable

**Observation :** wakeDocumentWaiters fait notify_all sur le CV commun à tous les documents et à la publication globale.

**Condition :** Nombreuses requêtes attendent des documents distincts pendant un gros build.

**Coût ou dette :** Jusqu’à O(P×D×Q) réévaluations de predicates pour P phases, D publications et Q waiters ; borne structurelle, pas fréquence mesurée.

**Usages et visibilité :** Builder waitUntil surchargé et protocoles ; le fast path sans waiter est déjà présent.

**Changement proposé :** Candidat : registres d’attente par document/rung ou regroupement de notifications, en gardant les annonces globales et échecs communes.

**Invariants à préserver :** Test du predicate et sommeil coordonnés sous mutex ; sortie Reached/Unreachable/Cancelled/BuildFailed et durée du document.

**Validation future — non exécutée :** Vérifier publication entre test/sleep, suppression, échec et annulation avec waiters mixtes ; mesurer réveils inutiles.

**Preuves dans la révision auditée :**

- [src/core/cursiv/workspace/DefaultDocumentBuilder.cpp:746](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/workspace/DefaultDocumentBuilder.cpp#L746) — if (_documentWaiters.load() == 0)
- [src/core/cursiv/workspace/DefaultDocumentBuilder.cpp:749](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/workspace/DefaultDocumentBuilder.cpp#L749) — _stateCv.notify_all();

<a id="finding-m-wsp-read-exit-notify"></a>
### M-WSP-READ-EXIT-NOTIFY — Le départ de chaque lecteur diffuse un réveil sans changement d’admission

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **WSP-014 — Admission d’une lecture workspace**. Périmètres associés : WSP-014.

**Frontière :** ReadAction, priorité et token → lecture protégée ou refus annulé

**Observation :** La sortie décrémente _readers/_inGate puis notify_all inconditionnel ; les writers attendent _readers==0, la destruction attend aussi zéro.

**Condition :** Beaucoup de lectures simultanées avec un writer ou teardown en attente.

**Coût ou dette :** Réveils et reprises du mutex pour des predicates encore faux à chaque lecteur intermédiaire.

**Usages et visibilité :** DefaultWorkspaceLock seulement ; les waiters Immediate dépendent de writeHeld/settled, pas du départ d’un autre lecteur.

**Changement proposé :** Conditionner ce broadcast aux transitions pertinentes, notamment dernier lecteur ; documenter les predicates affectés.

**Invariants à préserver :** Notifier avant de lâcher le mutex pour protéger la durée du CV ; conserver les autres notifications de settle, ticket et shutdown.

**Validation future — non exécutée :** Vérifier writers FIFO, Immediate, annulation de gate, exception action et destructeur ; mesurer contention.

**Preuves dans la révision auditée :**

- [src/core/cursiv/workspace/DefaultWorkspaceLock.cpp:67](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/workspace/DefaultWorkspaceLock.cpp#L67) — return _ticketServing == ticket && !_writeHeld && _readers == 0;
- [src/core/cursiv/workspace/DefaultWorkspaceLock.cpp:161](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/workspace/DefaultWorkspaceLock.cpp#L161) — --_inGate;

<a id="finding-m-wsp-importers-homonyms"></a>
### M-WSP-IMPORTERS-HOMONYMS — Revisiter les mêmes importers pour chaque homonyme

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **WSP-017 — Publication des symboles exportés**. Périmètres associés : WSP-017, WSP-019.

**Frontière :** Document parsé → ligne d’exports remplacée et vue invalidée

**Observation :** affectImporters visite la liste _importers[hash] pour chaque entrée de byShard ; old et fresh sont traités séparément.

**Condition :** Un document exporte H homonymes importés par I documents, ou exports inchangés entre versions.

**Coût ou dette :** O(H×I) insertions dans _affected pour le même ensemble d’IDs, plus répétition old/new.

**Usages et visibilité :** Hashes folded permettent faux positifs sûrs ; ils sont déjà stockés dans byShard.

**Changement proposé :** Dédupliquer les hashes d’exports par document pour cette seule opération, éventuellement union old/new.

**Invariants à préserver :** Conserver toutes les descriptions et leur ordre pour résolution ; collision doit continuer à sur-invalider plutôt que manquer une dépendance.

**Validation future — non exécutée :** Vérifier homonymes de types distincts, collisions et renommage ancien/nouveau ; mesurer gros groupes.

**Preuves dans la révision auditée :**

- [src/core/cursiv/workspace/DefaultIndexManager.cpp:245](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/workspace/DefaultIndexManager.cpp#L245) — for (const auto& entry : exports.byShard)
- [src/core/cursiv/workspace/DefaultIndexManager.cpp:143](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/workspace/DefaultIndexManager.cpp#L143) — affectImporters(*fresh);

<a id="finding-m-wsp-incoming-double-retire"></a>
### M-WSP-INCOMING-DOUBLE-RETIRE — Deux invalidations Incoming dans un même remplacement

**simplification · P3 · établi · confiance forte.** Périmètre principal : **WSP-018 — Mise à jour des références indexées**. Périmètres associés : WSP-018.

**Frontière :** Document lié → ligne de références et associations d’importateurs remplacées

**Observation :** Un updateReferences sur document déjà indexé passe par dropReferences qui retire le snapshot, puis appelle à nouveau _incoming.retire.

**Condition :** Réindexation avec ancienne row présente.

**Coût ou dette :** Double acquisition du writer mutex Published et éventuel scan des retraits/bandes.

**Usages et visibilité :** dropReferences sert aussi removeReferences/remove et doit y continuer à invalider.

**Changement proposé :** Séparer suppression de row et demande d’invalidation ou faire remonter un flag pour invalider une fois au niveau de l’opération.

**Invariants à préserver :** Chaque opération publique doit rendre le vieux snapshot inaccessible avant un lecteur suivant ; garder propriétaires durant callbacks.

**Validation future — non exécutée :** Vérifier remplacement, suppression seule et échec partiel d’allocation ; vérifier que la collecte différée reste déclenchable.

**Preuves dans la révision auditée :**

- [src/core/cursiv/workspace/DefaultIndexManager.cpp:174](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/workspace/DefaultIndexManager.cpp#L174) — dropReferences(document.id);
- [src/core/cursiv/workspace/DefaultIndexManager.cpp:239](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/workspace/DefaultIndexManager.cpp#L239) — if (had) _incoming.retire();

<a id="finding-m-wsp-export-subset"></a>
### M-WSP-EXPORT-SUBSET — La visite d’un sous-ensemble parcourt tous les documents exportés

**optimisation · P3 · candidat · confiance moyenne.** Périmètre principal : **WSP-020 — Construction du snapshot global d’exports**. Périmètres associés : WSP-020.

**Frontière :** Exports documentaires publiés → snapshot global ordonné et conservé pendant sa lecture

**Observation :** Le filtre documents est appliqué pendant le scan de view->rows, après allocation/tri de asked ; même un seul ID examine D rows.

**Condition :** Appels répétés avec petits sous-ensembles dans grands workspaces.

**Coût ou dette :** O(K log K + D log K + E retenus), et matérialisation K ; K=1 reste O(D).

**Usages et visibilité :** API IndexManager autorise liste vide=tout, ordre URI déterministe et callback réentrant.

**Changement proposé :** Candidat : index auxiliaire ID→row dans Exports ; pour petit K trouver/ordonner les rows sélectionnées selon ordre URI.

**Invariants à préserver :** Préserver déduplication implicite des IDs demandés et ordre exact de visite ; aucun lock tenu sur callback.

**Validation future — non exécutée :** Comparer liste vide, IDs absents/répétés et ordre des homonymes ; mesurer workload subset réel.

**Preuves dans la révision auditée :**

- [src/core/cursiv/workspace/DefaultIndexManager.cpp:284](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/workspace/DefaultIndexManager.cpp#L284) — for (const auto& [id, elements] : view->rows)
- [src/core/cursiv/workspace/DefaultIndexManager.cpp:285](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/workspace/DefaultIndexManager.cpp#L285) — std::ranges::binary_search(asked, id)

<a id="finding-m-wsp-incoming-build-lock"></a>
### M-WSP-INCOMING-BUILD-LOCK — La reconstruction complète des références entrantes tient le mutex global

**optimisation · P2 · candidat · confiance moyenne.** Périmètre principal : **WSP-021 — Lecture de l’index des références entrantes**. Périmètres associés : WSP-021.

**Frontière :** Cible documentaire et symbolId → visite des références triées

**Observation :** Allocation/copie de R descriptions et tri par cible se déroulent dans la section _mutex utilisée aussi par exports et writers.

**Condition :** Première navigation/références après une réindexation importante.

**Coût ou dette :** O(R + somme r_t log r_t) travail et allocations sous verrou ; pas de latence chiffrée.

**Usages et visibilité :** DefaultIndexManager garde actuellement Referenced par valeur, donc sortir du lock demande un ownership explicite.

**Changement proposé :** Candidat : capturer des rows immuables owning sous lock, construire hors lock puis publier si génération toujours actuelle.

**Invariants à préserver :** Snapshot intégral cohérent, ordre source déterministe et visiteurs réentrants ; ne pas fusionner invalidations Exports/Incoming.

**Validation future — non exécutée :** Vérifier deux builders de snapshot concurrents, nouvelle invalidation pendant build et lifetime des descriptions ; mesurer contention.

**Preuves dans la révision auditée :**

- [src/core/cursiv/workspace/DefaultIndexManager.cpp:363](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/workspace/DefaultIndexManager.cpp#L363) — for (const auto& [id, references] : _references)
- [src/core/cursiv/workspace/DefaultIndexManager.cpp:371](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/workspace/DefaultIndexManager.cpp#L371) — for (auto& [target, rows] : built->byTarget)

<a id="finding-m-wsp-initial-parse-serial"></a>
### M-WSP-INITIAL-PARSE-SERIAL — Le parsing initial précède le pool de build et reste séquentiel

**optimisation · P2 · candidat · confiance moyenne.** Périmètre principal : **WSP-025 — Initialisation et disponibilité du workspace**. Périmètres associés : MCP-017, WSP-025.

**Frontière :** Racines et documents additionnels → store installé, build initial et ready résolu

**Observation :** Le scan appelle factory.fromUri pour chaque fichier ; cette porte lit puis parse immédiatement avant l’ajout au store.

**Condition :** Ouverture d’un workspace contenant beaucoup de gros fichiers.

**Coût ou dette :** Somme des coûts de parse sur le thread initial avant la phase parallèle ; aucune vitesse relative mesurée.

**Usages et visibilité :** Le build incremental sait déjà installer des documents vides à Changed puis parse en parallèle ; factory hooks/ready contract diffèrent.

**Changement proposé :** Candidat : séparer admission des URI/buffers et analyse, ou paralléliser préparation hors store puis installer séquentiellement.

**Invariants à préserver :** Scan non interrompu voulu, document ouvert prioritaire, observabilité des fichiers illisibles, hooks et ready avant build.

**Validation future — non exécutée :** Comparer factories custom, ordre d’ajout/IDs, erreurs et annulation ; mesurer bootstrap mémoire et latence.

**Preuves dans la révision auditée :**

- [src/core/cursiv/workspace/DefaultDocumentFactory.cpp:58](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/workspace/DefaultDocumentFactory.cpp#L58) — parse(*document, services, cancelToken);
- [src/core/cursiv/workspace/DefaultWorkspaceManager.cpp:184](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/workspace/DefaultWorkspaceManager.cpp#L184) — resolveReady();

<a id="finding-m-ref-wait-list"></a>
### M-REF-WAIT-LIST — Les publications observées et cycles parcourent une liste globale

**optimisation · P2 · candidat · confiance moyenne.** Périmètre principal : **REF-010 — Attente et détection des cycles de résolution**. Périmètres associés : REF-010.

**Frontière :** Référence déjà en cours → attente annulable, résultat ou cycle signalé

**Observation :** publish parcourt toutes les Wait pour un seul state ; closes_cycle parcourt les waits et les chaînes de frames sous le même mutex.

**Condition :** Nombreux threads attendent des références différentes ou chaînes interdépendantes profondes.

**Coût ou dette :** Publication O(Q), cycle jusqu’à O(Q²×F) recherches de frames pour Q waits et longueur F ; chemin rare à mesurer.

**Usages et visibilité :** Coordinator de résolution uniquement ; il ne se remplace pas par notifier scheduler, car il détecte des cycles métier.

**Changement proposé :** Candidat : index state→waiters et index des frames de résolution pour accélérer wake/search ; conserver l’algorithme de cycle avant changement plus profond.

**Invariants à préserver :** Ordre d’enregistrement/publication, cycle trans-thread, cancellation callback hors lock initial, lifetime des Wait de pile.

**Validation future — non exécutée :** Vérifier cycles simples/croisés, erreur avec cible alternative et annulation/reclaim ; mesurer contention du slow path.

**Preuves dans la révision auditée :**

- [src/core/cursiv/syntax-tree/ReferenceResolution.cpp:88](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/syntax-tree/ReferenceResolution.cpp#L88) — for (auto* w = shared.head; w != nullptr; w = w->next)
- [src/core/cursiv/syntax-tree/ReferenceResolution.cpp:104](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/syntax-tree/ReferenceResolution.cpp#L104) — resolving_frame(w->chain, &current->target)

<a id="finding-m-ref-position-scans"></a>
### M-REF-POSITION-SCANS — La navigation par position rescane toutes les références

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **REF-013 — Recherche d’une déclaration à une position**. Périmètres associés : LSP-027, LSP-028, LSP-029, LSP-034, LSP-040, LSP-049, LSP-050, MCP-011, MCP-013, REF-013, REF-014.

**Frontière :** Document et offset → déclaration locale ou cibles de référence

**Observation :** Recherche d’un span résolu ou erroné et forçage local parcourent la worklist complète ; le retry peut reproduire les deux parcours.

**Condition :** Gros document avec beaucoup de références et requêtes au curseur fréquentes.

**Coût ou dette :** O(R) à plusieurs passes par requête, plus matérialisation du vector forceAt ; cst.leaves_before n’accélère pas ces scans.

**Usages et visibilité :** DocumentReferences est une interface remplaçable ; index additionnel doit être par parse, non global partagé entre versions.

**Changement proposé :** Candidat : index des spans vers handles lors du gel des références, query contenant offset puis résolution des seuls candidats.

**Invariants à préserver :** Plus petit span gagnant, bornes inclusive forceAt vs demi-ouvertes at, homonymes/pluriels et retry fin de nom.

**Validation future — non exécutée :** Vérifier spans chevauchants, erreurs de référence, deux nodes même CST et positions EOF ; mesurer navigation sur gros R.

**Preuves dans la révision auditée :**

- [src/core/cursiv/references/DefaultReferences.cpp:50](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/references/DefaultReferences.cpp#L50) — document.parseResult.references->visitResolved(
- [src/core/cursiv/references/DefaultReferences.cpp:73](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/references/DefaultReferences.cpp#L73) — document.parseResult.references->visitUnresolved(
- [src/core/cursiv/syntax-tree/DefaultDocumentReferences.cpp:87](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/syntax-tree/DefaultDocumentReferences.cpp#L87) — for (const RefHandle& h : handles) {

<a id="finding-m-val-spelling-topk"></a>
### M-VAL-SPELLING-TOPK — Les trois suggestions conservent et dédupliquent tous les noms proches

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **VAL-005 — Diagnostics de linking et suggestions orthographiques**. Périmètres associés : LSP-041, MCP-007, VAL-005.

**Frontière :** Références non résolues → diagnostics et suggestions de noms

**Observation :** near_names effectue un scan linéaire des noms déjà proches pour chaque candidat proche, puis trie la totalité avant resize(3).

**Condition :** Scope large avec nombreux noms à faible distance d’édition, ou plusieurs diagnostics demandés.

**Coût ou dette :** O(C×distance + K² + K log K) pour C candidats et K proches ; mémoire O(K), malgré top-3 final.

**Usages et visibilité :** Scope énuméré sans nom ; ordre distance, localité puis nom définit la sortie.

**Changement proposé :** Maintenir un petit top-3 ordonné et une déduplication adaptée, ou set de noms puis sélection partielle ; réutiliser calcul par site demandé plusieurs fois.

**Invariants à préserver :** Même sélection/dédup de noms, priorité locale, limites 64 sites et distance ; ne pas remplacer noms par seuls hashes.

**Validation future — non exécutée :** Comparer tie-breaks/case/homonymes locaux-globaux et plusieurs diagnostics sur même span ; mesurer scopes larges.

**Preuves dans la révision auditée :**

- [src/core/cursiv/validation/SpellingActions.cpp:46](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/validation/SpellingActions.cpp#L46) — std::vector<Near> near;
- [src/core/cursiv/validation/SpellingActions.cpp:65](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/validation/SpellingActions.cpp#L65) — std::ranges::sort(near
- [src/core/cursiv/validation/SpellingActions.cpp:30](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/validation/SpellingActions.cpp#L30) — kSpelledSites = 64

<a id="finding-m-svc-scheduler-comment"></a>
### M-SVC-SCHEDULER-COMMENT — Actualiser le contrat commenté des consommateurs du scheduler

**legacy · P3 · établi · confiance forte.** Périmètre principal : **SVC-001 — Ownership des conteneurs de services**. Périmètres associés : SVC-001.

**Frontière :** Conteneurs core/shared construits → services et dépendances détruits dans l’ordre

**Observation :** SharedCoreServices.hpp parle d’un seul consommateur, le builder, alors que DefaultLinker et DefaultDocumentValidator lancent des runs dans ce même pool.

**Condition :** Maintenance du graphe de services et décisions sur fan-out imbriqué.

**Coût ou dette :** Dette documentaire susceptible de faire retirer un support actif ; aucun coût runtime.

**Usages et visibilité :** Appels productifs trouvés dans linker et validator, pas seulement tests.

**Changement proposé :** Mettre à jour ce commentaire avec les trois consommateurs et leur règle de nesting ; aucun changement d’ownership nécessaire.

**Invariants à préserver :** Pool détruit après tous les utilisateurs ; TaskScheduler autorise les runs imbriqués et concurrents.

**Validation future — non exécutée :** Relecture des appels et du contrat public après correction documentaire.

**Preuves dans la révision auditée :**

- [src/core/cursiv/services/SharedCoreServices.hpp:39](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/services/SharedCoreServices.hpp#L39) — One consumer today
- [src/core/cursiv/references/DefaultLinker.cpp:138](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/references/DefaultLinker.cpp#L138) — shared.taskScheduler->parallelFor
- [src/core/cursiv/validation/DefaultDocumentValidator.cpp:473](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/validation/DefaultDocumentValidator.cpp#L473) — _core.shared().taskScheduler->parallelFor

<a id="finding-m-wsp-shard-document-scans"></a>
### M-WSP-SHARD-DOCUMENT-SCANS — Chaque shard reconstruit rescane toutes les rows documentaires

**optimisation · P3 · candidat · confiance moyenne.** Périmètre principal : **WSP-027 — Construction paresseuse des shards de noms exportés**. Périmètres associés : REF-005, WSP-027.

**Frontière :** Snapshot d’exports et nom cherché → shard publié et candidats filtrés

**Observation :** fill itère rows pour sommer, compter et scatter, en utilisant shardBegin pour chaque row ; aucune liste des rows contributrices par shard.

**Condition :** Beaucoup de petits documents et premiers lookups répartis sur plusieurs shards après invalidation.

**Coût ou dette :** O(S×D + E) scans documentaires pour S shards demandés, en plus du travail sur exports ; S borné par kShards.

**Usages et visibilité :** Index paresseux déjà efficace en gros documents ; bénéfice dépend de D/E et shards réellement demandés.

**Changement proposé :** Candidat : indexer les rows contributrices par shard pendant construction du snapshot, seulement si le coût/mémoire compense.

**Invariants à préserver :** Ordre URI puis export conservé ; construction indépendante des shards et ownership des rows.

**Validation future — non exécutée :** Mesurer workspaces multi-petits fichiers, faible et forte couverture de shards ; vérifier homonymes et collisions.

**Preuves dans la révision auditée :**

- [src/core/cursiv/workspace/DefaultIndexManager.cpp:89](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/workspace/DefaultIndexManager.cpp#L89) — for (const auto& [id, exported] : rows) count
- [src/core/cursiv/workspace/DefaultIndexManager.cpp:100](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/workspace/DefaultIndexManager.cpp#L100) — for (const auto& [id, exported] : rows | std::views::reverse)

<a id="finding-m-wsp-pending-rebuild"></a>
### M-WSP-PENDING-REBUILD — Déduplication et ordonnancement des documents reconstruits à chaque phase

**optimisation · P2 · établi · confiance forte.** Périmètre principal : **WSP-028 — Sélection et déduplication du travail d’une phase**. Périmètres associés : WSP-008, WSP-028, WSP-029.

**Frontière :** Documents proposés, état cible et options → lot de documents uniques à traiter

**Observation :** Chaque pending réserve deux conteneurs, insère les documents dans unordered_set puis partitionne les ouverts.

**Condition :** Build de nombreux documents, même lorsque le travail par phase est faible.

**Coût ou dette :** O(P×D) scans et allocations de hash nodes, plus buffers de stable_partition possibles.

**Usages et visibilité :** runPhase est utilisé avec work évolutif après contenu ; les phases doivent re-filtrer les états courants.

**Changement proposé :** Canonicaliser les pointeurs et leur ordre une fois après admission/extension de work, puis sélectionner les éligibles sans refaire le set.

**Invariants à préserver :** Doublons utilisateur exclus avant écriture parallèle ; ouverts d’abord de manière stable ; rebuild peut ajouter des documents.

**Validation future — non exécutée :** Vérifier duplicate input, document ajouté par publishContent, phases déjà franchies et validation désactivée ; compter allocations.

**Preuves dans la révision auditée :**

- [src/core/cursiv/workspace/DefaultDocumentBuilder.cpp:63](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/workspace/DefaultDocumentBuilder.cpp#L63) — seen.reserve(all.size());
- [src/core/cursiv/workspace/DefaultDocumentBuilder.cpp:82](https://github.com/ydaveluy/cursiv/blob/66c465bba3d2abd655e0e8fad3063f1fcfa9d599/src/core/cursiv/workspace/DefaultDocumentBuilder.cpp#L82) — const std::vector<Document*> work = pending(all, phaseEnd);
