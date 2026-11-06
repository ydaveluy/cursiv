# Cursiv — synthèse autonome pour investigation et correctifs

**Objet : reprendre l’investigation des constats et corriger ceux qui sont confirmés et pertinents pour la version de travail. Ce document ne nécessite ni l’historique de conversation, ni les JSON d’audit, ni un rapport précédent. Les sources du dépôt restent nécessaires à toute correction.**

Campagne statique commencée le 6 novembre 2026 : **513/513 périmètres examinés**, **96/96 comparaisons Langium documentées**, **186 constats distincts**. Les verdicts par périmètre sont : 192 avec constat, 314 sans constat établi et 7 avec réserve. Un constat peut concerner plusieurs périmètres.

Les fiches ci-dessous incluent les conditions, impacts, pistes de correction et preuves source. Les comparaisons sont incluses dans ce même fichier ; une absence dans Cursiv n’est pas automatiquement une régression ou une fonctionnalité à ajouter.

## Référentiel et reprise

| Dépôt | Révision auditée | Usage |
|---|---|---|
| https://github.com/ydaveluy/cursiv | `eabc0d70687bec94eb47e42e08b3987d4d3b8038` | Cible de l’audit |
| https://github.com/eclipse-langium/langium | `89d49680b39218af2e5b700cd3f4fdc122600ac4` — v4.4.0 | Référence de comparaison |
| https://github.com/ydaveluy/lsp-framework | `10d5d25721b22e06252f38c9fa93bf91dd702355` | Dépendance réellement examinée pour le runtime LSP |

Les chemins sans préfixe désignent la racine de Cursiv ; `langium/` désigne la référence et `deps/lsp-framework/` la dépendance. Les liens GitHub sont figés sur ces révisions. Les numéros de ligne ne doivent pas être appliqués aveuglément à HEAD.

Pour retrouver le référentiel, utiliser des clones séparés ou des worktrees propres, puis détacher ces commits. Ne pas remettre à zéro un checkout de travail. Un exemple de préparation dans un répertoire nouveau :

```sh
git clone https://github.com/ydaveluy/cursiv.git cursiv-audit-reference
git -C cursiv-audit-reference checkout --detach eabc0d70687bec94eb47e42e08b3987d4d3b8038
git clone https://github.com/eclipse-langium/langium.git langium-audit-reference
git -C langium-audit-reference checkout --detach 89d49680b39218af2e5b700cd3f4fdc122600ac4
git clone https://github.com/ydaveluy/lsp-framework.git lsp-framework-audit-reference
git -C lsp-framework-audit-reference checkout --detach 10d5d25721b22e06252f38c9fa93bf91dd702355
```

Les clones utilisés pendant la campagne sont sous `/media/data/workspaces/daveluy/git/audits/cursiv-2026-11-06/` si cet environnement est conservé. Ce chemin local n’est pas une dépendance de la passation.

## Consignes pour l’agent repreneur

1. Lire les `AGENTS.md` applicables dans le checkout cible. Noter son commit et comparer les zones concernées au snapshot audité : le dépôt peut déjà avoir corrigé ou déplacé un problème.
2. Choisir un petit lot par cause, en utilisant la clé stable du constat. Relire les sources citées, leurs appelants et les garanties publiques. Vérifier la condition avant toute modification.
3. Distinguer les catégories : un défaut peut appeler un patch local ; une limite peut nécessiter une borne ou une politique explicite ; un écart d’API peut rester un choix assumé. Ne pas engager toutes les évolutions API comme si elles étaient des réparations obligatoires.
4. Pour les réserves, établir l’invariant manquant avant de toucher l’algorithme. Ne pas promouvoir un état abstrait non prouvé atteignable en bug de production.
5. Respecter la demande d’investigation sans construire de reproducer. La campagne n’a exécuté aucun test, benchmark, build ou code Cursiv. Toute validation ultérieure doit être décrite comme un travail nouveau, avec son résultat réel, jamais comme une preuve déjà fournie par cet audit.
6. Préserver les sources et modifications de l’utilisateur. Préparer les corrections dans une branche ou un worktree isolé ; séparer les patches par cause et conserver la clé du constat dans la description.
7. Pour chaque issue, rendre : `confirmé / déjà corrigé / non retenu / choix API / réserve levée ou restante`, condition vérifiée, fichiers modifiés, contrat préservé, validation effectuée et limites. Ne pas annoncer un gain de performance à partir du seul changement de complexité.
8. Commencer par les lots prioritaires de la synthèse ci-dessous ; utiliser ensuite les index et les fiches. Le fichier est volontairement exhaustif : il peut être lu par sections sans charger tous les constats simultanément.

## Lecture des priorités et preuves

Priorités : **P1 1**, **P2 135**, **P3 50**. Catégories : 70 défauts, 57 limites, 46 écarts d’API, 13 risques.

P1 : sûreté ou contrat fondamental ; P2 : correction fonctionnelle ou limite opérationnelle ; P3 : durcissement ou ergonomie. La confiance porte sur ce que la lecture statique établit. Le scénario d’impact peut être conditionnel même lorsque le mécanisme fautif est certain. Les diagnostics « sans constat » ne constituent pas une preuve générale de correction.

**Navigation :** [synthèse technique](#synthèse-technique-et-ordre-dinvestigation), [index des constats](#index-des-constats), [fiches de correction](#fiches-pour-investigation-et-correction), [réserves](#réserves-à-lever), [comparaisons Langium](#comparaisons-langium-autonomes), [couverture des 513 périmètres](#couverture-des-513-périmètres).

## Synthèse technique et ordre d’investigation

La campagne distingue quatre sujets qui demandent des réponses différentes : correction d’un résultat faux, restauration d’une garantie de durée de vie ou d’exception, maîtrise du travail et de la mémoire, évolution du contrat public. Les priorités P1/P2/P3 expriment l’importance de ces garanties, pas une fréquence mesurée. Une limite d’API documentée peut rester un choix de produit ; elle ne justifie pas automatiquement une nouvelle fonctionnalité.

### Ordre conseillé pour les correctifs

| Lot | Points de départ | Pourquoi commencer ici | Contrats à préserver |
|---|---|---|---|
| 1 — Allocation et propriété | `CORE-BLOCK-ARITH`, `CORE-AST-COPY-TAG`, `WS-CAC-RETIRE-OWNERSHIP`, `WS-WSP-INDEX-RETIRE-ORDER` | Arithmétique d’allocation, identité dynamique et propriété après exception | Refus avant allocation/publication ; ordre de destruction CST/AST/texte ; vieux lecteurs protégés |
| 2 — Progression et arrêt | `HOST-WEB-WORKER-LOST-WAKE`, `WS-LSP-PENDING-SUBMIT-EXCEPTION`, `WS-CAC-SLOT-EXHAUSTION`, `WS-WSP-READ-REENTRANCE` | Chemins d’attente qui peuvent ne plus progresser | Équilibre des compteurs ; visibilité des publications ; jonction des tâches déjà publiées ; annulation |
| 3 — Fidélité des résultats | `CORE-TRANSLATION-ROUNDTRIP`, `COM-TYPO-INCOMPLET`, `LSP-FORMAT-KEYWORD-CASSE`, `WS-MCP-ROUTE-EMPTY-SUCCESS`, `WS-MCP-VALIDATE-CATEGORIES`, `WS-MCP-WORKSPACE-REFERENCE-BARRIER` | Résultat absent, incomplet ou négatif présenté comme définitif | Conserver les positions moteur, l’état de coupure, la phase réellement lue et le contexte de langue |
| 4 — Revalidation métier | `EXA-HERITAGE-REVALIDATION`, `EXA-REQ-COUVERTURE-PERIMEE` | Dépendances transitives ou inverses dépassant les invalidations directes | Ne pas généraliser une dépendance métier en politique globale coûteuse sans besoin établi |
| 5 — Bornes opérationnelles | `CORE-COMPLETION-BOUNDS`, `LL-SIMULATION-BUDGET-DIFFERE`, `LR-DP-TRAVAIL-NON-FACTURE`, `COM-ELECTION-HORS-QUOTA` | Les quotas présents ne couvrent pas toutes les opérations | Préserver la validité des bornes et distinguer résultat complet, coupé et annulé |
| 6 — Livraison et génération | `BLD-SCAFFOLD-EXT-NONBORNEE`, `BLD-PACKAGE-RAFRAICHISSEMENT`, `EXA-GENERATION-ECRITURE`, `CORE-GEN-PARTIAL-PUBLISH` | Projet produit incorrect ou succès annoncé après écriture incomplète | Identité exacte des artefacts, absence de troncature involontaire et reporting des erreurs |
| 7 — Évolutions de framework | `API-*`, `CORE-GRAMMAR-RUNTIME-MODEL`, `LSP-SEMANTIC-LEGENDE-FIXE` | Coût de portage et extensibilité | Décider le contrat avant de multiplier les adaptateurs ou d’exposer des détails moteur |

Les conditions des fiches restent déterminantes. Le P1 d’allocation vise de très grandes demandes, surtout sur 32 bits/Wasm. Plusieurs défauts d’exception exigent un échec d’allocation **puis la conservation et réutilisation de l’objet**. Le cycle de verrou suppose une extension réentrante ; il n’est pas imputé aux providers par défaut. Aucun de ces points ne doit être converti en affirmation de panne courante sans preuve supplémentaire.

### Moteurs LL et LR : chemins à distinguer

Les noms LL/LR ne suffisent pas à comparer les coûts. Les deux moteurs possèdent une chaîne de génération, un chemin de lecture ordinaire, des mécanismes de choix ou de partage, un réparateur et un calcul de continuations au caret. Le budget d’un étage ne borne pas implicitement les autres.

| Étage | LL : résultats de l’audit | LR : résultats de l’audit |
|---|---|---|
| Grammaire et génération | FIRST, fins indirectes, alias d’opérateurs, DFA et suffixes de retour examinés. Le seuil 512 de DFA est testé avant expansion, donc pas un plafond strict des états alloués. Les chaînes d’alias et suffixes communs peuvent être reparcourus quadratiquement. | Lowering, items/fermetures, lookaheads, lanes, tables et émission examinés. Des traversées restent récursives. `optionalGate` et la taille de groupes sont protégés par la CLI mais pas de façon équivalente dans leurs API internes. |
| Chemin courant et piles | Contrôles de tailles de `LlMachine` relus : `_used` et `_room` sont en `size_t`, avec refus lié à `kNoCall`. L’ancienne hypothèse d’un simple rebouclage uint32 de ces compteurs n’est pas retenue. | Les piles partagées conservent des identités exactes et des pools stables ; les arènes runtime possèdent néanmoins plusieurs conversions vers IDs 32 bits sans refus release à l’échelle extrême. |
| Gestion des alternatives | Cellules, callers, historiques, frontier et mémos sont distingués. Historiques sous forme d’arbres équilibrés et comparaison exacte après hash ; réserve séparée sur l’ordre des historiques effectivement atteignables. | Le GSS partage des préfixes de pile mais ne rend pas toutes les opérations linéaires : acceptation de chemins, comparaison des shifts et fixpoints globaux peuvent dominer le coût. |
| Réparation | Une fermeture `LlSimulation` sans limite interne peut être drainée avant paiement au budget extérieur. La borne globale peut donc être constatée trop tard pour borner cette unité de travail. | La programmation dynamique de borne, la spéculation et la collecte de sources ont des quotas de portées différentes. Certaines transitions de la borne ne sont pas facturées. |
| Complétion | Rejeu du préfixe et réévaluation des conditions sans contexte public d’annulation/budget. | Même manque à la frontière publique ; hypothèses différées, caches linéaires et échantillons lexicaux ajoutent des limites spécifiques. |

**LL — rétention et complexité.** Les buffers TLS d’épisode conservent leur capacité maximale par thread. Cela se multiplie avec le nombre de threads ayant exécuté le moteur ; ce n’est pas une contention sur un cache partagé. Le front trié déplace ses éléments lors d’insertions et le contrôle d’appartenance des callers rescane leurs vecteurs. Le lookup examine au plus 16 candidats par chaîne ; cette limite ne borne ni les entrées conservées, ni les plans stockés, ni les octets comparés. Les constats `LL-EPISODE-TLS-RETENTION`, `LL-FRONT-DEPLACEMENTS`, `LL-CELLULE-APPELS-SCAN` et `LL-MEMO-VOLUME` décrivent ces dimensions séparément.

**LR — partage et combinatoire.** Avec `F` arrêts gelés, le plafond local 512 peut fournir jusqu’à `512 × F` sources de réparation : ce n’est pas une limite 512 pour toute la recherche. Avec `S` shifts candidats, l’élection inspectée compare chaque shift aux autres, donc un coût quadratique peut subsister malgré le partage des piles. L’acceptation peut parcourir tous les chemins d’un graphe partagé ; le nombre de chemins est distinct du nombre de nœuds. Les chaînes d’hypothèses de complétion sont internées dans l’ordre d’ajout : des ordres différents peuvent garder des identités différentes pour un même ensemble sémantique. Ces constats établissent des mécanismes de croissance, sans chiffre de ralentissement.

**LR — mémoire de génération et de complétion.** La croissance des pools de graphe peut réserver presque 16 fois la taille utilisée. Le seuil de trim du cache d’ordre écrit ne couvre pas toutes ses structures. Le cache d’échantillons du caret peut matérialiser `modes × terminaux` dès sa première utilisation. En revanche, la construction de `LrProgramLayout` parcourt bien `états × (terminaux + non-terminaux)` avec **une seule ligne de travail dense** ; l’audit ne retient pas une matrice dense intégralement résidente. Le rapport ne confond pas temps de parcours et mémoire simultanée.

**Réparation commune — incomplétude.** `COM-TYPO-INCOMPLET` est une seule cause partagée : au désaccord lexical, certaines avances fantômes/substitutions sont limitées à quatre successeurs. Le signal `refused()` ne rejoint pas `cut/shortOf` chez les adaptateurs concernés. Les mémos peuvent donc conserver un résultat tronqué sans ce statut. En LR, le signal manque aussi avant consommation dans la borne DP. La perte du signal est établie ; aucune dérivation démontrant une réparation finale sous-optimale ou un élagage incorrect concret n’a été produite. La correction doit préserver une borne conservatrice, pas simplement augmenter le chiffre 4.

**Complétion — résultats et annulation.** `CORE-COMPLETION-BOUNDS` regroupe les deux moteurs : corriger la signature commune sans sonder les boucles internes ne suffit pas. Il faut transmettre le contexte jusqu’aux fermetures, replays et essais lexicaux, et préciser l’issue partielle. `CORE-TRANSLATION-ROUNDTRIP` est indépendant : des spans moteur projetés sur une pièce source remplacée deviennent indiscernables ; le replay LR exige des bornes exactes que la projection inverse ne peut reconstruire. Conserver les positions moteur répond à ce problème de fidélité, sans dépendre du budget.

**Quotas réellement observables.** Cursiv possède `RepairBudget` et `DiagnosticKind::bounded`. L’audit ne prétend donc pas que toute coupure est silencieuse. Il identifie des opérations non couvertes, des signaux perdus et l’absence d’options stables dans `Parser`/`create_parser()`. `RepairBudget` borne des unités de travail instrumentées ; il ne définit ni échéance murale, ni plafond de mémoire globale, ni quota de complétion.

### Caches : contention, progression et restitution mémoire

Il faut distinguer la table concurrente, la gestion globale des epochs, les snapshots `Published` et les caches document/workspace. Les mêmes mots « cache » ou « sans mutex côté lecteur » ne leur donnent pas les mêmes garanties.

| Mécanisme | Synchronisation relue | Où le travail se concentre | Conséquence et piste d’investigation |
|---|---|---|---|
| Lookup `ConcurrentTable` | Guard d’epoch puis chargements atomiques ; publication du payload par ready release/acquire | Premier accès d’un thread : inscription globale. Copie de `V` tant que le guard existe | Une copie ou un callback long prolonge la protection et peut retarder la collecte de caches étrangers |
| Insertion | CAS du groupe ; comparaison exacte des clés ; attente d’un slot de même tag non encore ready | Mêmes groupes/tags et producteurs retardés | La progression d’une insertion dépend du producteur qui doit publier ready ; ne pas conclure à une garantie wait-free |
| Croissance | Mutex par table pour installer le successeur, puis blocs copiés coopérativement et compteurs atomiques | Compteurs claimed/copied ; attente du dernier helper ; slots pas encore ready | La copie répartie n’élimine pas l’attente. Préserver le scellement et la propriété des Nodes pendant toute modification |
| Retrait par epochs | Mutex **global** et liste commune, avance d’epoch, scan des inscriptions ; membarrier si disponible | Retraits de tables de caches distincts ; collecte effectuée sous ce mutex | Un lecteur ancien retient des retraits d’autres caches. Les destructeurs sont exécutés après libération du mutex global |
| Snapshots `Published` | 64 stripes ; compteur seq_cst par lecture ; mutex propre aux publications | Collisions de threads sur la même stripe, scan des 64 stripes et liste retirée | L’alignement réduit le faux partage entre stripes ; il ne supprime pas les collisions dans une stripe. La sûreté d’un vieux lecteur reste conservée |
| `DocumentCache` | Cache TLS des partitions, registre stable et mutex de création | Premier accès concurrent à un nouveau DocumentId | Le double contrôle évite le doublon publié, mais les partitions restent conservées pour stabiliser les pointeurs TLS |
| `SimpleCache` | Provider calculé hors guard/verrou de table, puis insertion premier gagnant | Plusieurs misses de la même clé peuvent calculer simultanément | Calcul dupliqué possible, distinct de la contention du CAS. Le provider doit être déterministe pour sa clé |

**1024 inscriptions, pas 1024 lecteurs actifs.** Les places d’epoch sont rendues à la mort du thread, pas à la fin d’une lecture. Après 1024 threads vivants ayant chacun utilisé un cache, un nouveau thread peut tourner même si les 1024 autres ne lisent plus. Le plafond `CURSIV_WORKERS=1024` ne réserve pas ces places pour les threads d’appel, autres schedulers et réponses LSP. Le remède concerne le cycle de vie/admission des inscriptions, pas seulement un backoff plus lent.

**8 Mio n’est pas une borne de mémoire retenue.** Le seuil compte les allocations des tables, pas les Nodes ni les allocations détenues par `V`. La collecte se déclenche au bout de 32 retraits **ou** de 8 Mio de tables comptées ; les anciens lecteurs peuvent encore la retarder. Leur sortie ne déclenche pas de collecte à elle seule. Un payload important peut rester détenu sous ces seuils, ou après qu’une collecte précédente a été bloquée. C’est une rétention, à distinguer de la perte irréversible d’ownership si l’insertion du retrait lève après détachement de la table.

**`Published` protège son propre commit, pas le commit de son appelant.** Il inscrit l’ancien propriétaire avant de publier le nouveau pointeur ; une allocation échouée laisse donc son ancien snapshot cohérent. Mais `DefaultIndexManager` peut avoir déjà modifié ses tables quand il appelle retire. Si l’exception est récupérée, la vue publiée peut alors diverger des tables mutées ; un reset ultérieur peut rendre les données empruntées pendantes. La correction doit traiter la transaction index+invalidation. Changer isolément les ordres mémoire du lecteur n’est pas la réponse à ce défaut.

**Fraîcheur.** Une génération de cache empêche de mémoriser certains calculs commencés avant clear, mais ne garantit pas que le provider a lu un document à la bonne phase. De même, les listeners de `DocumentCache` suivent les dépendances prévues par leur contrat, pas toutes les dépendances métier transitives possibles. La barrière workspace et les dépendances du validateur restent nécessaires.

### Scheduler, verrous de workspace et dispatch LSP

Le pool de calcul Cursiv et le pool de réponses de `lsp-framework` sont distincts. Le runtime choisit jusqu’à `max(1, ceil(P/2))` workers de réponse lorsque `TaskScheduler::defaultWorkerCount()` vaut `P>0`, alors que le scheduler peut créer `P` workers de calcul. S’ajoutent les threads de contrôle/notifications selon l’hôte. `P` n’est donc pas un plafond global des threads du processus. À 0, le traitement des requêtes sur la boucle de lecture empêche cette même boucle de recevoir une annulation pendant le calcul. Sources de raccord : `TaskScheduler.cpp:20,39` et `LanguageServerRuntime.cpp:975`.

**Découpage du travail.** `parallelFor` exige un range de taille connue/random-access pour le chemin parallèle. Le pool distribue au plus `workers+1` parts, le demandeur participant au travail. Les grandes réservations utilisent approximativement `reste / (2 × participants)`, puis le mode fin prend les derniers indices un à un. Une tranche déjà revendiquée n’est pas volable séparément : si ses derniers éléments sont coûteux, d’autres workers peuvent attendre malgré du travail encore local à cette tranche. Le tri des documents ouverts en tête influence l’ordre de départ, pas une garantie d’achèvement prioritaire.

**Publication et exceptions.** Les queues workers ont un producteur propriétaire ; les producteurs externes utilisent des buckets protégés par mutex. Plusieurs demandeurs externes commencent par les premiers buckets et premières cellules, créant des points de convergence. La comptabilité de publication partielle et la jonction avant destruction de la tâche sur pile sont cohérentes dans `WorkerPool`. Ce résultat positif ne s’étend pas au compteur distinct `PendingRequests` du runtime LSP : il est incrémenté avant une soumission de réponse susceptible de lever, et son garde de sortie ne s’exécute que si la tâche commence.

**Attente ordinaire et attente imbriquée.** Le worker ordinaire annonce son intention de dormir, réexamine les queues puis valide l’attente ; le dernier élément d’une deque est arbitré par CAS. Aucun défaut de réveil analogue à celui du navigateur n’a été établi ici. En revanche, le worker qui attend un sous-job continue les scans et yield pour pouvoir aider ce sous-job ; lorsque seuls des callbacks longs restent actifs, ces scans consomment du travail sans progresser. Le demandeur externe, lui, passe à atomic::wait après son nombre de tours prévu. C’est une différence de politique de progression, à préserver si l’on réduit le spin.

**Annulation.** `TaskScheduler` capture l’exception d’un premier callback et cesse le travail utile des wrappers après échec/annulation. Le compteur de réservation du pool ne s’arrête pas pour autant : les indices restants sont encore parcourus. Il faut distinguer « aucun nouveau traitement métier » de « retour immédiat de parallelFor ». Les callbacks déjà actifs, les scans lexicaux, les conversions et les fermetures de moteur ont leur propre granularité d’annulation.

**Verrou workspace.** Une lecture Normal attend les writers pendants ; une lecture Immediate peut entrer après la mutation du writer actif mais bloque les écritures suivantes tant qu’elle reste ouverte. Une priorité ne garantit donc pas une borne d’attente sous flux continu. Reprendre Normal depuis un lecteur existant après arrivée d’un writer crée le cycle lecteur→writer→lecteur. Un worker de build qui reprend Normal peut attendre le writer qui attend sa jonction. Le cycle dure tant qu’aucune annulation/fermeture ne le rompt. Les extensions doivent connaître leur contexte d’accès ; le protocole de base non imbriqué n’est pas déclaré incorrect.

**Notifications.** La queue LSP préserve l’ordre des notifications antérieures à une requête, mais attend la fin de leur callback complet, potentiellement tout le build. Elle peut ainsi allonger la latence même si un provider ne demande qu’une phase précoce. Inversement, le helper MCP de références/rename attend seulement un document avant de lire l’index global ; cette barrière est trop faible pour des écritures concurrentes. La solution est une exigence de phase correcte par opération, pas la suppression générale des barrières.

### Langium : fonctionnalités, contrats et adaptations

La référence est **Langium 4.4.0 au commit indiqué dans ce document**. Une comparaison d’interface ne prouve ni supériorité de performance, ni meilleure sûreté générale de l’une des implémentations. Les 96 réponses détaillent présence, branchement et comportement effectif.

| Besoin de portage | Situation Cursiv | Ce que fournit Langium, avec ses propres limites |
|---|---|---|
| Codec AST | Pas de service générique serialize/deserialize ; le visiteur d’enfants ne suffit pas pour valeurs, absence, discriminants et restauration | JsonSerializer avec options, URI et chemins de références ; pas un CST complet restitué ni un codec automatique de tous types spéciaux |
| Modèle synthétique | Factory de nœud isolé disponible ; intégration arena/registre/références/document à réaliser | fromModel produit un document Parsed et attache la racine ; il n’invente ni CST ni positions et suppose un modèle cohérent |
| Provenance du code généré | TextBuilder émet le texte ; SourceTranslation traite une autre direction, avant parsing | traceToNode/toStringAndTrace et régions imbriquées ; le consommateur doit conserver les traces des fichiers produits |
| Coloration lexicale | TextMate maintenu manuellement, explicitement documenté | Générateurs TextMate/Monarch/Prism best-effort ; aucune équivalence générale au parsing |
| Notebook | Textes virtuels possibles, pas de relation/cycle de cellules fourni | NormalizedNotebookDocuments existe ; nécessite branchement/annonce de capacités et ne définit pas les scopes métier |
| Fragment par règle d’entrée | Entrée fixée dans le parseur généré, Parser.parse sans sélection | ParserOptions.rule sélectionne une règle nommée ; le type générique attendu reste la responsabilité de l’appelant |
| Politique de récupération | Budget interne configurable par les types moteurs, factory commune sans option stricte | IParserConfig public transmet recoveryEnabled à Chevrotain ; ce n’est pas une politique par appel ni un quota équivalent |
| Réflexion de grammaire | Questions effacées centrées sur CST/terminaux ; pas de Grammar AST structurel public complet | Grammar disponible comme modèle inspectable par les services |
| Scopes et références | Tags C++, bitsets, narrowing et signatures d’extension propres ; casse ASCII des noms | Types nommés et conversion Unicode JS ; un portage direct des hooks ne préserve pas toutes les conventions |
| Semantic tokens | Légende partagée fixe 8 types, sans modificateur configurable par langue ni API delta | Surface de provider plus riche ; le delta reste lui aussi désactivé par défaut dans la référence inspectée |

Des différences importantes sont aussi **partagées** : certains comportements de priorité de formatting, gestion des commentaires et sélection de plages existent dans les deux implémentations. La complétion synchrone Langium ne résout pas automatiquement les sections non interruptibles. Deux helpers abstraits Langium de navigation accumulent même des liens sans les retourner dans le snapshot comparé ; les fiches concernées l’indiquent au lieu de considérer la présence d’une classe comme preuve de fonctionnement supérieur.

### Duplications à traiter par contrat commun

La bonne unité de factorisation est le comportement dont les copies ont déjà divergé. Les lecteurs de mémoire Wasm Node/web ont des profils et validations différents ; les routes LSP/MCP ne construisent pas toujours le même contexte de scope ni la même barrière d’index ; les conversions Monaco perdent des champs que le protocole serveur conserve. Les extensions VSCode recopient le résolveur et le bridge, et le template maintient encore une variante de sélection. Des générateurs d’exemple répètent le contrôle incomplet d’écriture finale.

À l’inverse, toutes les structures se ressemblant ne doivent pas être fusionnées : epochs de `ConcurrentTable` et stripes de `Published` protègent des usages différents ; les frontiers LL et caches LR ont des clés et coûts différents ; les trois problèmes d’exception de registry, validation et index ont des commits distincts. Le regroupement des constats conserve ces frontières afin de proposer des correctifs isolables.

### Limites et réserves de cette campagne

Les constats contiennent leurs conditions et preuves source ; les sept réserves contiennent l’invariant qui manque. Elles portent sur la dominance de chemins, l’ordre d’historiques atteignables, une croissance lexicale non caractérisée, la réutilisation après exception, la supervision de démarrage JS et un format LCOV effectivement émis. Elles ne sont pas des défauts démontrés cachés sous un autre nom. Un agent chargé des corrections doit lever l’invariant manquant avant de modifier l’algorithme concerné.

Les propositions sont des directions de correction, pas des patches validés. Aucune mesure de performance, exécution de test, compilation ou nouveau reproducer n’a été réalisé. Les comparaisons de complexité désignent les dimensions parcourues dans les sources ; elles ne prédisent pas à elles seules le coût dominant d’un langage particulier.

## Index des constats

| Priorité | Catégorie | Clé et objet | Périmètres |
|---|---|---|---|
| P1 | défaut | [CORE-BLOCK-ARITH — L’arène ne borne pas l’addition et l’arrondi de la taille d’allocation](#finding-core-block-arith) | AST-001, CST-001 |
| P2 | écart d’API | [API-AST-CODEC — Pas de service public de sérialisation réversible de l’AST](#finding-api-ast-codec) | API-001 |
| P2 | écart d’API | [API-ENTRY-RULE — La règle d’entrée n’est pas sélectionnable par appel public](#finding-api-entry-rule) | API-006 |
| P2 | écart d’API | [API-FROM-MODEL — L’intégration d’un modèle synthétique exige une extension d’ownership](#finding-api-from-model) | API-002 |
| P2 | écart d’API | [API-GENERATION-TRACE — Le texte généré ne conserve pas de carte de provenance](#finding-api-generation-trace) | API-003 |
| P2 | écart d’API | [API-RECOVERY-POLICY — La politique de récupération n’est pas portable via Parser](#finding-api-recovery-policy) | API-007 |
| P2 | défaut | [ASS-AST-OPTIONAL-SENTINEL — Une valeur optionnelle absente est indiscernable du texte '(unset)'](#finding-ass-ast-optional-sentinel) | ASS-007 |
| P2 | défaut | [ASS-CODEACTION-URI-EFFACEE — Le helper applique au document courant les edits de toutes les URI](#finding-ass-codeaction-uri-effacee) | ASS-005 |
| P2 | défaut | [ASS-CST-SOUSARBRE-TRAVERSE — L’oracle CST du fuzzer accepte un enfant débordant du sous-arbre parent](#finding-ass-cst-sousarbre-traverse) | ASS-006 |
| P2 | défaut | [ASS-DIAGNOSTIC-ANTERIEUR — La session LSP accepte un diagnostic antérieur à l’ouverture qu’elle observe](#finding-ass-diagnostic-anterieur) | ASS-020 |
| P2 | défaut | [ASS-LISTE-CARDINALITE-EFFACEE — La comparaison peut accepter des listes de cardinalités différentes](#finding-ass-liste-cardinalite-effacee) | ASS-003 |
| P2 | limite | [ASS-RGRAM-CAMPAGNE-VIDE — Une campagne Rgram vide ou sans aucun arbre jugé peut réussir](#finding-ass-rgram-campagne-vide) | ASS-011 |
| P2 | défaut | [ASS-SESSION-ARRET-IGNORE — Trois clients de session peuvent réussir après arrêt forcé ou sortie en erreur du serveur](#finding-ass-session-arret-ignore) | ASS-020 |
| P2 | défaut | [BLD-DISTRIBUTION-ANCIENS-MODULES — Une sortie de distribution réutilisée peut embarquer un module absent du build courant](#finding-bld-distribution-anciens-modules) | BLD-010 |
| P2 | défaut | [BLD-PACKAGE-RAFRAICHISSEMENT — Une modification isolée du runner ou une copie supprimée ne rafraîchit pas vscode/server](#finding-bld-package-rafraichissement) | BLD-010 |
| P2 | défaut | [BLD-SCAFFOLD-EXT-NONBORNEE — EXT n’est ni échappée ni limitée à un suffixe de fichier](#finding-bld-scaffold-ext-nonbornee) | BLD-014 |
| P2 | limite | [COM-ELECTION-HORS-QUOTA — L’élection finale commune poursuit des parcours hors quota et sans annulation](#finding-com-election-hors-quota) | COM-006, LR-058, LR-059 |
| P2 | défaut | [COM-TYPO-INCOMPLET — Le plafond de branchement typo n’est pas propagé comme une exploration incomplète](#finding-com-typo-incomplet) | COM-012, LL-038, LR-056, LR-057 |
| P2 | défaut | [CORE-AST-COPY-TAG — Une copie par base peut conserver le tag du type dérivé](#finding-core-ast-copy-tag) | AST-003 |
| P2 | limite | [CORE-COMPLETION-BOUNDS — La frontière de complétion LL/LR ne transporte ni annulation ni budget](#finding-core-completion-bounds) | LL-046, LR-062, LR-065, LR-066 |
| P2 | limite | [CORE-CONVERSION-POLL — Les parcours de conversion textuelle ne sondent pas l’annulation](#finding-core-conversion-poll) | AST-010 |
| P2 | risque | [CORE-GEN-PARTIAL-PUBLISH — Un échec de génération peut laisser un ensemble de fichiers partiellement renouvelé](#finding-core-gen-partial-publish) | GRA-012 |
| P2 | défaut | [CORE-GRAMMAR-OVERRIDE-DIAMOND — Un override local ne résout pas le conflit entre deux overrides parents](#finding-core-grammar-override-diamond) | GRA-002 |
| P2 | écart d’API | [CORE-GRAMMAR-RUNTIME-MODEL — L’API effacée n’expose pas le modèle structurel complet de la grammaire](#finding-core-grammar-runtime-model) | GRA-007 |
| P2 | écart d’API | [CORE-TEXT-SIZE-DOMAIN — TextDocument accepte un texte hors du domaine de ses offsets 32 bits](#finding-core-text-size-domain) | TXT-004 |
| P2 | défaut | [CORE-TRANSLATION-ROUNDTRIP — La complétion perd des positions après une traduction non inversible](#finding-core-translation-roundtrip) | CST-007, LR-065 |
| P2 | défaut | [CORE-UNICODE-OUTPUT-PATH — Le générateur Unicode vise un répertoire absent de l’arborescence actuelle](#finding-core-unicode-output-path) | TXT-003 |
| P2 | défaut | [CORE-URI-LOCALHOST — localhost ne disparaît pas de la clé URI malgré le contrat public](#finding-core-uri-localhost) | TXT-009 |
| P2 | limite | [CORE-URI-TRIE-DEPTH — Le trie d’URI conserve une profondeur de pile proportionnelle au chemin](#finding-core-uri-trie-depth) | TXT-010 |
| P2 | limite | [EXA-ARITH-PROFONDEUR — Les parcours arithmétiques reprennent une pile native proportionnelle à la profondeur](#finding-exa-arith-profondeur) | EXA-002, EXA-004, EXA-005, EXA-006 |
| P2 | défaut | [EXA-CLI-EXCEPTIONS — Les sous-commandes des exemples échappent à la gestion d’erreurs commune](#finding-exa-cli-exceptions) | EXA-002, EXA-013, EXA-030, EXA-031 |
| P2 | défaut | [EXA-CYCLES-ARGUMENTS — La détection de récursion ignore les appels présents dans les arguments](#finding-exa-cycles-arguments) | EXA-004 |
| P2 | limite | [EXA-DOMAIN-PROFONDEUR — La qualification multiplie les noms stockés et les parcours récursifs](#finding-exa-domain-profondeur) | EXA-009, EXA-011, EXA-013 |
| P2 | défaut | [EXA-EVALUATION-SILENCIEUSE — Une évaluation impossible disparaît de la sortie avec un code de succès](#finding-exa-evaluation-silencieuse) | EXA-002 |
| P2 | défaut | [EXA-GENERATION-ECRITURE — Les trois générateurs annoncent la réussite sans vérifier l’écriture finale](#finding-exa-generation-ecriture) | EXA-013, EXA-030, EXA-031 |
| P2 | défaut | [EXA-HERITAGE-REVALIDATION — Un cycle d’héritage introduit dans un ancêtre éloigné ne revalide pas tous ses descendants](#finding-exa-heritage-revalidation) | EXA-010 |
| P2 | défaut | [EXA-JAVA-IDENTITES — Les types Java émis ne suivent pas l’identité résolue du modèle](#finding-exa-java-identites) | EXA-012, EXA-013 |
| P2 | défaut | [EXA-JAVA-NOMS — Les noms et collisions admissibles dans le modèle peuvent produire du Java invalide](#finding-exa-java-noms) | EXA-012 |
| P2 | défaut | [EXA-JAVA-PACKAGE-CHEMIN — Le package Java dérive d’un chemin insuffisamment normalisé](#finding-exa-java-package-chemin) | EXA-013 |
| P2 | défaut | [EXA-NORMALISATION-SYNTAXE — La normalisation émet des nombres que la grammaire ne peut pas relire](#finding-exa-normalisation-syntaxe) | EXA-006 |
| P2 | défaut | [EXA-PARAMETRE-CALLABLE — Un paramètre peut être appelé avec des arguments ignorés](#finding-exa-parametre-callable) | EXA-002, EXA-028 |
| P2 | écart d’API | [EXA-REQ-CHAINES-LANGIUM — Les chaînes de requirements conservent leurs échappements, contrairement à Langium](#finding-exa-req-chaines-langium) | EXA-015 |
| P2 | défaut | [EXA-REQ-COUVERTURE-PERIMEE — Modifier les tests ne rafraîchit pas les diagnostics de couverture des exigences](#finding-exa-req-couverture-perimee) | EXA-017 |
| P2 | défaut | [EXA-REQ-RAPPORT-ERREURS — Le rapport est annoncé réussi malgré des erreurs de modèle ou de tests](#finding-exa-req-rapport-erreurs) | EXA-019, EXA-031 |
| P2 | limite | [EXA-SM-ACTIONS-OMISES — Les commandes et actions du modèle disparaissent du C++ généré](#finding-exa-sm-actions-omises) | EXA-025 |
| P2 | écart d’API | [EXA-SM-COPIE-OWNERSHIP — La machine générée est copiable malgré un état possédé par pointeur brut](#finding-exa-sm-copie-ownership) | EXA-024 |
| P2 | défaut | [EXA-SM-NOMS-CPP — Les noms acceptés ne sont pas protégés contre les collisions C++ générées](#finding-exa-sm-noms-cpp) | EXA-022 |
| P2 | défaut | [EXA-SM-REFERENCES-EXTERNES — La génération accepte des cibles interdocuments qu’elle ne déclare pas](#finding-exa-sm-references-externes) | EXA-021, EXA-023, EXA-026 |
| P2 | défaut | [HOST-MCP-LISTEN-ERROR — L’erreur d’écoute du bridge MCP n’a pas de gestionnaire](#finding-host-mcp-listen-error) | HOST-022 |
| P2 | écart d’API | [HOST-WEB-COMPLETION-PARTIAL — L’adaptateur Monaco omet les modifications annexes des complétions](#finding-host-web-completion-partial) | HOST-015 |
| P2 | défaut | [HOST-WEB-EDIT-VERSION — Les préconditions de version des WorkspaceEdit sont perdues](#finding-host-web-edit-version) | HOST-017 |
| P2 | défaut | [HOST-WEB-FRAME-FAILURE — Une trame web invalide ne clôt pas les requêtes en attente](#finding-host-web-frame-failure) | HOST-010 |
| P2 | défaut | [HOST-WEB-POLL-DEADLINES — poll_oneoff annonce aussi les horloges dont le délai n’est pas échu](#finding-host-web-poll-deadlines) | HOST-008 |
| P2 | défaut | [HOST-WEB-RING-SIGNED — L’anneau stdin utilise des indices négatifs après 2 Gio cumulés](#finding-host-web-ring-signed) | HOST-009 |
| P2 | défaut | [HOST-WEB-SESSION-ERROR — Les erreurs asynchrones de démarrage ne suivent pas toutes la session active](#finding-host-web-session-error) | HOST-013 |
| P2 | défaut | [HOST-WEB-WORKER-LOST-WAKE — Le pool web peut perdre le réveil qui démarre un pthread](#finding-host-web-worker-lost-wake) | HOST-011 |
| P2 | écart d’API | [HOST-WINDOWS-NONEXISTENT-PATH — Les arguments de chemins Windows non existants ne sont pas traduits](#finding-host-windows-nonexistent-path) | HOST-003 |
| P2 | limite | [LEX-ANNULATION-TOKEN — Plusieurs scans d’un token long ne consultent pas l’annulation](#finding-lex-annulation-token) | LEX-025 |
| P2 | limite | [LL-SIMULATION-BUDGET-DIFFERE — Une fermeture de réparation échappe au budget externe et à l’annulation](#finding-ll-simulation-budget-differe) | LL-041 |
| P2 | limite | [LR-CARET-CACHE-DENSE — Le cache d’échantillons alloue modes × terminaux dès sa première utilisation](#finding-lr-caret-cache-dense) | LR-067 |
| P2 | limite | [LR-CARET-ECHANTILLONS-INCOMPLETS — Les neuf échantillons fixes ne garantissent pas l’exhaustivité des terminaux conditionnels](#finding-lr-caret-echantillons-incomplets) | LR-066 |
| P2 | limite | [LR-CARET-HYPOTHESES-COMBINATOIRES — Les conditions différées et les chaînes ordonnées peuvent multiplier les frames de complétion](#finding-lr-caret-hypotheses-combinatoires) | LR-063 |
| P2 | limite | [LR-DP-TRAVAIL-NON-FACTURE — Les transitions de la borne dynamique ne sont pas toutes facturées au budget de travail](#finding-lr-dp-travail-non-facture) | LR-057, LR-058 |
| P2 | écart d’API | [LR-GROUP-LINES-ZERO — L’API directe d’émission accepte une taille de groupe nulle puis divise par zéro](#finding-lr-group-lines-zero) | LR-036 |
| P2 | limite | [LR-GSS-ACCEPT-CHEMINS — L’acceptation déroule tous les chemins de pile partagés avant de choisir une dérivation](#finding-lr-gss-accept-chemins) | LR-047, LR-050 |
| P2 | limite | [LR-GSS-ELECTION-SHIFTS-QUADRATIQUE — L’élection lexicale du GSS compare chaque shift à tous les autres](#finding-lr-gss-election-shifts-quadratique) | LR-052 |
| P2 | limite | [LR-GSS-READ-PAST-GLOBAL — Le calcul des progrès de réparation rescane toutes les arêtes jusqu’au fixpoint](#finding-lr-gss-read-past-global) | LR-051 |
| P2 | limite | [LR-GSS-SOURCES-CAP-LOCAL — Le plafond de 512 sources de réparation s’applique à chaque arrêt gelé, pas au lot global](#finding-lr-gss-sources-cap-local) | LR-051 |
| P2 | risque | [LR-LOWERING-PROFONDEUR — Des parcours du lowering restent récursifs selon la profondeur de grammaire](#finding-lr-lowering-profondeur) | LR-003 |
| P2 | écart d’API | [LR-OPTIONAL-GATE-INTERNE — optionalGate n'est validé que par la CLI](#finding-lr-optional-gate-interne) | GRA-006, LR-004 |
| P2 | limite | [LR-POOLS-CROISSANCE-16 — Les pools de graphe réservent jusqu'à presque seize fois leur taille](#finding-lr-pools-croissance-16) | LR-011 |
| P2 | risque | [LR-RUNTIME-IDS32-SANS-GARDE — Plusieurs arènes runtime tronquent leurs IDs à 32 bits sans refus en release](#finding-lr-runtime-ids32-sans-garde) | LR-045, LR-046, LR-047, LR-053, LR-062 |
| P2 | limite | [LR-SPECULATION-QUOTAS-PARTIELS — Les quotas de spéculation ne bornent pas son front initial ni toutes ses comparaisons](#finding-lr-speculation-quotas-partiels) | LR-043, LR-044 |
| P2 | limite | [LR-WRITTEN-CACHE-PARTIEL — Le seuil trim ne borne pas la mémoire totale de l'ordre écrit](#finding-lr-written-cache-partiel) | LR-016 |
| P2 | limite | [LSP-FORMAT-HIDDEN-ESPACES — prepend sur un commentaire ignore les demandes d’espaces et de nouvelles lignes](#finding-lsp-format-hidden-espaces) | LSP-037 |
| P2 | défaut | [LSP-FORMAT-KEYWORD-CASSE — Les sélecteurs de formatting perdent les variantes de casse de ikw](#finding-lsp-format-keyword-casse) | CST-005, EXA-007, EXA-014, EXA-020, EXA-027, LSP-035 |
| P2 | limite | [LSP-FORMAT-PLAGE-DEBUT — Un chevauchement sur le début de la sélection peut exclure tout un sous-arbre](#finding-lsp-format-plage-debut) | LSP-038 |
| P2 | défaut | [LSP-FORMAT-PRIORITE-GAP — La priorité ne départage pas deux règles visant le même gap par des côtés différents](#finding-lsp-format-priorite-gap) | LSP-036 |
| P2 | écart d’API | [LSP-SEMANTIC-LEGENDE-FIXE — Le provider ne peut pas déclarer ses propres catégories ou modificateurs sémantiques](#finding-lsp-semantic-legende-fixe) | LSP-039 |
| P2 | limite | [WS-CAC-DEFERRED-PAYLOAD — La rétention des valeurs retirées n’est pas bornée par le seuil de 8 Mio](#finding-ws-cac-deferred-payload) | CAC-008 |
| P2 | écart d’API | [WS-CAC-FUNCTOR-CONTRACT — Les caches imposent des contraintes non exprimées aux hashers personnalisés](#finding-ws-cac-functor-contract) | CAC-001 |
| P2 | défaut | [WS-CAC-RETIRE-OWNERSHIP — Une exception d’enregistrement du retrait perd définitivement l’ancienne table](#finding-ws-cac-retire-ownership) | CAC-007 |
| P2 | limite | [WS-CAC-SLOT-EXHAUSTION — Les 1024 inscriptions TLS peuvent bloquer un nouvel utilisateur du cache](#finding-ws-cac-slot-exhaustion) | CAC-006 |
| P2 | défaut | [WS-LSP-ACTION-DOCUMENTCHANGES-OVERWRITE — Le versionnement écrase les documentChanges fournis par une action](#finding-ws-lsp-action-documentchanges-overwrite) | LSP-041 |
| P2 | écart d’API | [WS-LSP-CLIENT-RESULT-ONLY — Les requêtes sortantes n’exposent ni erreur ni terminaison garantie](#finding-ws-lsp-client-result-only) | LSP-009 |
| P2 | écart d’API | [WS-LSP-COMPLETION-TRIGGER-CONTEXT — Le provider de complétion perd triggerKind et triggerCharacter](#finding-ws-lsp-completion-trigger-context) | LSP-023 |
| P2 | limite | [WS-LSP-FOLDING-CANCEL-POLL — Le parcours de repliement ne sonde l’annulation que lors des résultats](#finding-ws-lsp-folding-cancel-poll) | LSP-032 |
| P2 | écart d’API | [WS-LSP-HIERARCHY-ITEM-IDENTITY — Le helper de hiérarchie ne préserve pas l’identité après une édition](#finding-ws-lsp-hierarchy-item-identity) | LSP-049, LSP-050 |
| P2 | défaut | [WS-LSP-INITIALIZE-FLAG — Les notifications personnalisées passent après un initialize échoué](#finding-ws-lsp-initialize-flag) | LSP-007 |
| P2 | limite | [WS-LSP-INLINE-CANCELLATION — Le mode sans workers ne peut pas traiter l’annulation pendant une requête](#finding-ws-lsp-inline-cancellation) | LSP-005 |
| P2 | limite | [WS-LSP-JSON-PRECISION — Les payloads JSON int64 perdent leur précision au passage LSP](#finding-ws-lsp-json-precision) | LSP-022 |
| P2 | limite | [WS-LSP-NOTIFICATION-BARRIER — La barrière de notifications attend le build complet](#finding-ws-lsp-notification-barrier) | LSP-004 |
| P2 | écart d’API | [WS-LSP-NOTIFICATION-ERROR-SINK — Les échecs des notifications sont perdus](#finding-ws-lsp-notification-error-sink) | LSP-004 |
| P2 | défaut | [WS-LSP-PENDING-SUBMIT-EXCEPTION — Une soumission de réponse échouée peut bloquer le shutdown](#finding-ws-lsp-pending-submit-exception) | LSP-006 |
| P2 | écart d’API | [WS-LSP-SAVE-HOOKS — Les hooks typés de sauvegarde sont absents de la surface serveur](#finding-ws-lsp-save-hooks) | LSP-053 |
| P2 | écart d’API | [WS-LSP-SERVICE-REQUIREMENT-EXTENSION — Le prérequis de phase est codé dans les wrappers du serveur](#finding-ws-lsp-service-requirement-extension) | LSP-019 |
| P2 | écart d’API | [WS-LSP-SESSION-REUSE — État de session conservé lors de la réutilisation des services](#finding-ws-lsp-session-reuse) | LSP-002 |
| P2 | limite | [WS-LSP-WORKSPACE-FOLDER-CHANGES — Les changements de racines ne mettent pas à jour le workspace par défaut](#finding-ws-lsp-workspace-folder-changes) | LSP-015 |
| P2 | défaut | [WS-MCP-BOUND-LANGUAGE — Les outils MCP retrouvent une langue par URI et perdent le languageId du document](#finding-ws-mcp-bound-language) | MCP-009, MCP-013 |
| P2 | écart d’API | [WS-MCP-CATALOG-DUPLICATE — Le catalogue MCP peut publier plusieurs entrées de même nom mais n’en appeler qu’une](#finding-ws-mcp-catalog-duplicate) | MCP-002 |
| P2 | écart d’API | [WS-MCP-FORMAT-DOCUMENT-URI — Le formatter appelé par MCP reçoit une URI de document vide](#finding-ws-mcp-format-document-uri) | MCP-019 |
| P2 | défaut | [WS-MCP-OUTLINE-UNNAMED-CONTAINERS — Le plan MCP omet les déclarations sous des conteneurs anonymes](#finding-ws-mcp-outline-unnamed-containers) | MCP-010 |
| P2 | limite | [WS-MCP-RENAME-QUADRATIC-DEDUP — La déduplication du renommage MCP effectue un nombre quadratique de comparaisons](#finding-ws-mcp-rename-quadratic-dedup) | MCP-012 |
| P2 | limite | [WS-MCP-REQUEST-ID-RANGE — Les IDs numériques MCP sont limités à int32 par la couche LSP](#finding-ws-mcp-request-id-range) | MCP-001 |
| P2 | défaut | [WS-MCP-ROUTE-EMPTY-SUCCESS — Une lecture MCP non exécutée devient un résultat vide réussi](#finding-ws-mcp-route-empty-success) | MCP-004, MCP-007, MCP-011, MCP-012, MCP-013, MCP-014, MCP-015 |
| P2 | défaut | [WS-MCP-SCOPE-CONTEXT — scope-candidates construit un contexte différent de celui de la complétion LSP](#finding-ws-mcp-scope-context) | MCP-015 |
| P2 | défaut | [WS-MCP-SCOPE-FIRST-SITE — scope-candidates ignore toutes les alternatives après le premier site reconnu](#finding-ws-mcp-scope-first-site) | MCP-015 |
| P2 | limite | [WS-MCP-SCRATCH-IDENTITY-RETENTION — Des noms de fichiers successifs font croître indéfiniment les identités temporaires](#finding-ws-mcp-scratch-identity-retention) | MCP-005 |
| P2 | défaut | [WS-MCP-SCRATCH-RELEASE-ALLOCATION — La restitution d’un slot peut terminer le processus en cas d’allocation échouée](#finding-ws-mcp-scratch-release-allocation) | MCP-005 |
| P2 | limite | [WS-MCP-STDIO-DISK-SNAPSHOT — Le serveur MCP stdio ne renouvelle pas les fichiers après son démarrage](#finding-ws-mcp-stdio-disk-snapshot) | MCP-017 |
| P2 | écart d’API | [WS-MCP-SYMBOLS-MISSING-CST — symbols indexe le CST sans vérifier le fallback symbolId](#finding-ws-mcp-symbols-missing-cst) | MCP-009 |
| P2 | défaut | [WS-MCP-URI-NONHIERARCHICAL — Les URI sans // sont transformées en chemins de fichier](#finding-ws-mcp-uri-nonhierarchical) | MCP-003 |
| P2 | défaut | [WS-MCP-VALIDATE-CATEGORIES — validate omet les catégories lentes tout en annonçant le texte valide](#finding-ws-mcp-validate-categories) | MCP-006 |
| P2 | défaut | [WS-MCP-VALIDATE-CLEANUP-FAILURE — Un échec du nettoyage peut laisser un scratch détenu après libération du slot](#finding-ws-mcp-validate-cleanup-failure) | MCP-006 |
| P2 | défaut | [WS-MCP-VALIDATE-READ-ADMISSION — validate peut annoncer un texte valide sans avoir lu un état Validated](#finding-ws-mcp-validate-read-admission) | MCP-006 |
| P2 | limite | [WS-MCP-VALIDATE-SHARED-EXPORTS — Les validations temporaires ne sont pas isolées entre appels](#finding-ws-mcp-validate-shared-exports) | MCP-006 |
| P2 | défaut | [WS-MCP-WORKSPACE-REFERENCE-BARRIER — Les références et le renommage MCP peuvent lire un index global encore incomplet](#finding-ws-mcp-workspace-reference-barrier) | MCP-011, MCP-012 |
| P2 | limite | [WS-REF-CASEFOLD-ASCII — Le scope insensible à la casse ne rapproche pas les lettres non ASCII](#finding-ws-ref-casefold-ascii) | REF-005, TXT-002 |
| P2 | écart d’API | [WS-REF-DESCRIPTION-NODE-REQUIRED — Une description identifiée mais sans pointeur AST ne peut pas être liée par le service par défaut](#finding-ws-ref-description-node-required) | REF-007 |
| P2 | écart d’API | [WS-REF-ERROR-CAUSE-LOST — Le linking perd les causes d’erreur personnalisées et les exceptions des providers](#finding-ws-ref-error-cause-lost) | REF-009 |
| P2 | défaut | [WS-REF-OVERLOAD-SPAN-BEGIN — La récupération d’un groupe de surcharges confond des sites ayant le même début](#finding-ws-ref-overload-span-begin) | REF-014 |
| P2 | écart d’API | [WS-REF-SCOPE-EXTENSION-DRIFT — Une politique de scope personnalisée doit rester cohérente dans trois chemins d’accès](#finding-ws-ref-scope-extension-drift) | REF-006 |
| P2 | limite | [WS-SCH-CANCEL-LINEAR — L’annulation conserve un parcours linéaire des indices restants](#finding-ws-sch-cancel-linear) | SCH-003 |
| P2 | limite | [WS-SCH-NESTED-SPIN — Une attente imbriquée peut entretenir des scans actifs sans travail disponible](#finding-ws-sch-nested-spin) | SCH-009 |
| P2 | défaut | [WS-SVC-REGISTRATION-PARTIAL — Une inscription interrompue peut rester routable tout en disparaissant de all](#finding-ws-svc-registration-partial) | SVC-005 |
| P2 | écart d’API | [WS-SVC-SHARED-MISMATCH — Le registry accepte une langue construite pour un autre workspace](#finding-ws-svc-shared-mismatch) | SVC-004 |
| P2 | défaut | [WS-VAL-CATEGORY-COMMIT — Une exception après publication des diagnostics peut laisser les catégories non acquises](#finding-ws-val-category-commit) | VAL-009 |
| P2 | défaut | [WS-VAL-MISSING-ANCHOR-RANGE — Une ancre absente écrase la plage déjà choisie par le builder](#finding-ws-val-missing-anchor-range) | VAL-008 |
| P2 | défaut | [WS-VAL-REGISTRY-HASH-EXCEPTION — Un échec de rehash peut laisser le registre avec un multiplicateur incompatible avec sa table](#finding-ws-val-registry-hash-exception) | VAL-001 |
| P2 | limite | [WS-VAL-SPELLING-QUADRATIC — La sélection de trois suggestions peut comparer quadratiquement tous les noms proches](#finding-ws-val-spelling-quadratic) | VAL-005 |
| P2 | défaut | [WS-WSP-INDEX-RETIRE-ORDER — Une invalidation qui lève peut laisser un snapshot périmé après mutation de l’index](#finding-ws-wsp-index-retire-order) | WSP-017 |
| P2 | écart d’API | [WS-WSP-LOCAL-NULL-CONTAINER — Un conteneur nul accepté par LocalSymbols::add rend seal invalide](#finding-ws-wsp-local-null-container) | WSP-022 |
| P2 | écart d’API | [WS-WSP-PENDING-OPTIONS — Un build de reprise peut ignorer la nouvelle demande de validation](#finding-ws-wsp-pending-options) | WSP-007 |
| P2 | limite | [WS-WSP-PRIORITY-STARVATION — Les priorités n’offrent pas de borne de progression sous flux continu](#finding-ws-wsp-priority-starvation) | WSP-014 |
| P2 | écart d’API | [WS-WSP-READ-REENTRANCE — La réentrance de lecture peut fermer un cycle d’attente](#finding-ws-wsp-read-reentrance) | WSP-014 |
| P2 | défaut | [WS-WSP-SYMLINK-INCLUSION — Le scan initial et les événements peuvent sélectionner différemment un lien symbolique](#finding-ws-wsp-symlink-inclusion) | TXT-011, WSP-024 |
| P3 | écart d’API | [API-HIGHLIGHT-GENERATION — La coloration lexicale livrée est maintenue séparément de la grammaire](#finding-api-highlight-generation) | API-004 |
| P3 | écart d’API | [API-NOTEBOOK-LIFECYCLE — Pas de gestionnaire natif du cycle notebook/cellules](#finding-api-notebook-lifecycle) | API-005 |
| P3 | limite | [ASS-CPU-IGNORE-SILENCIEUX — Le contrôle de croissance CPU LR peut être ignoré sans trace dans le verdict](#finding-ass-cpu-ignore-silencieux) | ASS-019 |
| P3 | défaut | [ASS-MARQUEURS-MALFORMES — Des marqueurs de plage mal appariés sont normalisés sans erreur](#finding-ass-marqueurs-malformes) | ASS-001 |
| P3 | défaut | [ASS-REPAIR-JSON-BUDGET — Le JSON des métriques de réparation omet le budget utilisé](#finding-ass-repair-json-budget) | ASS-014 |
| P3 | limite | [ASS-RSS-ETAT-PIC — Le pic RSS est publié sans indiquer si sa lecture et sa remise à zéro ont réussi](#finding-ass-rss-etat-pic) | ASS-023 |
| P3 | limite | [BLD-DEPENDANCES-NPM-HORS-SUIVI — La mise à jour automatisée déclarée ne couvre pas les paquets npm du dépôt](#finding-bld-dependances-npm-hors-suivi) | BLD-020 |
| P3 | défaut | [BLD-FRAMES-ARGUMENTS-INVALIDES — Le contrôle de frames réussit avec un seuil non numérique ou sans module](#finding-bld-frames-arguments-invalides) | BLD-023 |
| P3 | écart d’API | [BLD-IDENTITE-VIRGULE — FILE_NAMES et EXTENSIONS perdent les virgules appartenant à une valeur](#finding-bld-identite-virgule) | BLD-004 |
| P3 | risque | [CGR-OPTION-CROISSANCE — La réécriture des longues suites optionnelles matérialise des suffixes quadratiques](#finding-cgr-option-croissance) | CGR-007 |
| P3 | risque | [CGR-SPELL-ALIAS — Le speller d’opérateurs parcourt récursivement ses alias avec une recherche linéaire de cycle](#finding-cgr-spell-alias) | CGR-008 |
| P3 | limite | [CGR-UNARY-LIMITE — Des niveaux d’opérateurs disjoints peuvent être refusés faute d’énumération](#finding-cgr-unary-limite) | CGR-009 |
| P3 | défaut | [CGR-WARN-IDENTITE — La déduplication des conditions non bornées utilise un rendu non injectif](#finding-cgr-warn-identite) | CGR-014 |
| P3 | défaut | [CLI-DUMP-UTF8-COUPE — La troncature du dump humain peut couper un caractère UTF-8](#finding-cli-dump-utf8-coupe) | CLI-005 |
| P3 | défaut | [COM-CHAINS-SEUIL-DECALAGE — Le plafond annoncé des chaînes diffère d’une unité du seuil appliqué](#finding-com-chains-seuil-decalage) | COM-021 |
| P3 | limite | [COM-DYNSET-COUT-CREUX — Le coût d’itération de DynSet dépend aussi de l’étendue des IDs](#finding-com-dynset-cout-creux) | COM-018 |
| P3 | écart d’API | [CORE-JSON-UTF8 — JsonValue écrit les octets non UTF-8 sans politique explicite](#finding-core-json-utf8) | PUB-005 |
| P3 | défaut | [CORE-OBS-NUL — Une observation contenant NUL est tronquée sur stderr](#finding-core-obs-nul) | PUB-007 |
| P3 | limite | [CORE-UNICODE-VERSION — Les tables Unicode ne portent pas une version de génération reproductible](#finding-core-unicode-version) | TXT-003 |
| P3 | limite | [EXA-REQ-RAPPORT-COUT — Le rapport relit toutes les références de tests pour chaque exigence](#finding-exa-req-rapport-cout) | EXA-019 |
| P3 | limite | [EXA-SM-TRANSITIONS-COUT — La déduplication des transitions est quadratique par état](#finding-exa-sm-transitions-cout) | EXA-023 |
| P3 | écart d’API | [HOST-EXTENSION-DUPLICATION — La sélection de serveur est dupliquée avec des priorités difficiles à maintenir](#finding-host-extension-duplication) | HOST-019 |
| P3 | écart d’API | [HOST-MEMORY-READER-DRIFT — Les deux lecteurs d’imports Wasm ont divergé](#finding-host-memory-reader-drift) | HOST-002 |
| P3 | écart d’API | [HOST-WEB-FD-THREAD-LOCAL — Le filesystem WASI web ne partage pas les descripteurs entre pthreads](#finding-host-web-fd-thread-local) | HOST-007 |
| P3 | écart d’API | [HOST-WEB-PLAINTEXT-MARKDOWN — Les contenus LSP plaintext sont interprétés comme Markdown](#finding-host-web-plaintext-markdown) | HOST-016 |
| P3 | risque | [LEX-FRONT-INSERTIONS — Insertion quadratique possible des curseurs concurrents](#finding-lex-front-insertions) | LEX-020 |
| P3 | risque | [LL-CELLULE-APPELS-SCAN — L’admission des callers d’une cellule rescane ses membres](#finding-ll-cellule-appels-scan) | LL-030 |
| P3 | risque | [LL-DFA-CAP-DEPASSEMENT — Le plafond de 512 états n’est pas un plafond d’allocation](#finding-ll-dfa-cap-depassement) | LL-011 |
| P3 | risque | [LL-EPISODE-TLS-RETENTION — Les buffers d’épisode retiennent leur capacité maximale par thread](#finding-ll-episode-tls-retention) | LL-026 |
| P3 | risque | [LL-FRONT-DEPLACEMENTS — Le front trié d’épisode a un coût quadratique en positions coexistantes](#finding-ll-front-deplacements) | LL-032 |
| P3 | risque | [LL-MEMO-VOLUME — La borne de 16 candidats ne borne ni le stockage ni les octets comparés](#finding-ll-memo-volume) | LL-033 |
| P3 | risque | [LL-OP-ALIAS-CROISSANCE — Résolution d’une chaîne d’alias d’opérateur quadratique et récursive](#finding-ll-op-alias-croissance) | LL-005 |
| P3 | limite | [LL-PRODUCTIVITE-EOF-INDIRECT — Diagnostic de règle improductive incomplet après une fin indirecte](#finding-ll-productivite-eof-indirect) | LL-008 |
| P3 | risque | [LL-RETURN-SUFFIX-RESCAN — Les classes de retour reparcourent les suffixes epsilon communs](#finding-ll-return-suffix-rescan) | LL-010 |
| P3 | limite | [LR-CARET-CACHES-LINEAIRES — Plusieurs caches et déduplications de complétion sont parcourus linéairement](#finding-lr-caret-caches-lineaires) | LR-064, LR-066 |
| P3 | limite | [LR-GROUP-FRONT-RESCANS — Le partitionnement des fonctions rescane un front potentiellement large à chaque placement](#finding-lr-group-front-rescans) | LR-036 |
| P3 | défaut | [LSP-FORMAT-COMMENT-IDEMPOTENCE — Un espace après le marqueur de commentaire peut imposer deux formatages successifs](#finding-lsp-format-comment-idempotence) | EXA-007, EXA-014, LSP-037 |
| P3 | écart d’API | [LSP-SEMANTIC-DELTA-EXTENSION — L’API ne permet pas à une langue d’activer les semantic tokens delta](#finding-lsp-semantic-delta-extension) | LSP-040 |
| P3 | écart d’API | [WS-CAC-BOOTSTRAP-NOEXCEPT — Une allocation initiale du gestionnaire d’epochs ne peut pas remonter comme bad_alloc](#finding-ws-cac-bootstrap-noexcept) | CAC-006 |
| P3 | limite | [WS-CAC-DOCUMENT-PARTITIONS — Le cache conserve une partition pour chaque identité documentaire rencontrée](#finding-ws-cac-document-partitions) | CAC-012 |
| P3 | limite | [WS-CAC-SNAPSHOT-RETENTION — Le dernier snapshot retiré peut rester détenu après la sortie de tous ses lecteurs](#finding-ws-cac-snapshot-retention) | CAC-010 |
| P3 | écart d’API | [WS-LSP-NAVIGATION-NULL-ERASED — Le serveur efface la distinction nullopt/liste vide des providers de navigation](#finding-ws-lsp-navigation-null-erased) | LSP-042 |
| P3 | limite | [WS-LSP-PLAINTEXT-MARKUP — L’adaptation en texte brut laisse les marqueurs Markdown visibles](#finding-ws-lsp-plaintext-markup) | LSP-034 |
| P3 | défaut | [WS-MCP-DOCUMENT-SOURCE-LABEL — L’inventaire annonce sur disque tout document fermé, même virtuel](#finding-ws-mcp-document-source-label) | MCP-008 |
| P3 | limite | [WS-MCP-OUTLINE-DEPTH — Le plan MCP est tronqué silencieusement à 17 niveaux](#finding-ws-mcp-outline-depth) | MCP-010 |
| P3 | limite | [WS-SCH-EXTERNAL-CONVOY — Les demandeurs externes convergent sur les mêmes premiers points de contention](#finding-ws-sch-external-convoy) | SCH-006 |
| P3 | limite | [WS-SCH-PRIVATE-SLICE — Une tranche réservée reste indivisible sur une charge hétérogène](#finding-ws-sch-private-slice) | SCH-005 |
| P3 | écart d’API | [WS-VAL-ANCHOR-NOEXCEPT — La recherche d’une ancre indexée alloue dans une méthode noexcept](#finding-ws-val-anchor-noexcept) | VAL-008 |
| P3 | écart d’API | [WS-VAL-OPTIONS-OPTIN — Une surcharge des options de validation nécessite aussi un second opt-in](#finding-ws-val-options-optin) | VAL-007 |
| P3 | écart d’API | [WS-WSP-WAIT-CANCELLED-BUILD — Annuler un build ne termine pas les waiters dépourvus de leur propre annulation](#finding-ws-wsp-wait-cancelled-build) | WSP-026 |

## Fiches pour investigation et correction

Chaque fiche est une unité de travail possible. Les corrections proposées sont des directions à vérifier, pas des patches déjà testés. Les invariants locaux examinés dans les périmètres concernés sont ajoutés pour éviter de casser une garantie en corrigeant une autre.

<a id="finding-core-block-arith"></a>

### CORE-BLOCK-ARITH — L’arène ne borne pas l’addition et l’arrondi de la taille d’allocation

**P1 · défaut · confiance forte.** Périmètres : AST-001, CST-001.

**Mécanisme identifié.** BlockArena calcule sizeof(Block)+align+bytes puis std::bit_ceil(room) sans vérifier la représentabilité de l’addition ni de la puissance de deux suivante. Un débordement peut conduire à réserver moins de mémoire que la demande ; au-delà de la plus grande puissance représentable, bit_ceil sort de son domaine défini. Le plafond de records du CST protège cap*sizeof(CstNode), mais ne réserve pas la place du header et de l’alignement ajoutés en aval.

**Condition à vérifier.** Très grandes réservations, particulièrement en espace d’adressage 32 bits/Wasm. La borne CstRun kByBytes peut encore transmettre une taille proche de SIZE_MAX ; aucun échec d’allocation anticipé n’est garanti par ces additions.

**Impact.** Le contrat d’allocation ne garantit plus un bloc couvrant les écritures demandées. La panne effective dépend de la taille atteinte et de l’allocateur ; aucun crash n’a été provoqué.

**Direction de correction.** Valider bytes contre SIZE_MAX moins l’overhead, puis choisir un arrondi représentable ou une allocation exacte. Refuser la demande par bad_alloc/overflow_error avant toute publication du bloc.

**Sources à relire.**

- [src/core/cursiv/syntax-tree/BlockArena.cpp:28](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/syntax-tree/BlockArena.cpp#L28) — Addition room et bit_ceil dans do_allocate.
- [src/core/cursiv/syntax-tree/CstRun.hpp:212](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/syntax-tree/CstRun.hpp#L212) — Demande de cap*sizeof(CstNode) à la ressource.
- [src/core/cursiv/syntax-tree/CstRun.hpp:225](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/syntax-tree/CstRun.hpp#L225) — Borne fondée sur les seuls octets des records.

**Contexte AST-001 — Allocation des blocs alignés.** Les blocs ordinaires doublent jusqu’à 4 Mio, les demandes supérieures à la moitié du plafond prennent un bloc dédié et chaque bloc mémorise taille/alignement pour sa restitution. L’ownership est installé après allocate : une exception de l’upstream ne perd pas le bloc précédent. En revanche, la somme header+alignement+payload et bit_ceil ne sont pas protégés. Le calcul at+bytes avant comparaison de capacité forme également potentiellement un pointeur au-delà du bloc ; un contrôle par distance serait nécessaire même avant d’examiner les tailles extrêmes.

**Limite de preuve de ce périmètre.** La fréquence réelle et le seuil d’échec de l’allocateur hôte ne sont pas mesurés. La violation du domaine numérique est établie par les expressions, indépendamment d’une exécution.

**Contexte CST-001 — Croissance et invalidation des vues du flux CST.** CstRun borne le nombre de records par l’espace des NodeId et par SIZE_MAX/sizeof(CstNode). La réallocation réserve avant de déplacer les records et ne remplace _r/_cap qu’après allocation réussie. En mode PMR les anciens buffers restent dans l’arène ; hors PMR ils sont libérés. Les moves retirent le watch d’annulation de la source. Le contrôle local du produit ne suffit cependant pas à protéger l’overhead de l’allocateur sous-jacent, ce qui rattache ce consommateur au défaut AST-001.

**Limite de preuve de ce périmètre.** Les pointeurs nus vers les records sont invalidés par une croissance ; les IDs survivent seulement aux append sans insertion intermédiaire. Aucun volume RSS n’est déduit de la seule capacité.

<a id="finding-api-ast-codec"></a>

### API-AST-CODEC — Pas de service public de sérialisation réversible de l’AST

**P2 · écart d’API · confiance forte.** Périmètres : API-001.

**Mécanisme identifié.** JsonValue représente des données JSON possédées, mais ne connaît ni types AST, ni références, ni rattachement au document. AstReflection expose noms et références ; visitMembers visite les enfants non nuls, y compris les alternatives AST actives des variants, mais ne décrit pas les valeurs scalaires, états absents, cardinalités déclarées, discriminants et accès en écriture nécessaires à un codec générique. Les traits C++ peuvent servir à construire un codec propre au langage ; aucun contrat serialize/deserialize ne le fournit dans les services inspectés.

**Condition à vérifier.** Persistance de modèles ou échange avec un outil qui attend le contrat JsonSerializer de Langium.

**Impact.** Le consommateur doit construire les codecs de valeurs C++, les identifiants de types, la représentation des références interdocuments et leur restauration. Un dump JSON du CST ne répond pas à ce besoin.

**Direction de correction.** Définir un service optionnel de codec AST avec métadonnées de valeurs, hooks pour types C++ non JSON, références par URI/chemin et contrat de restauration/documentation des données non conservées.

**Sources à relire.**

- [src/core/cursiv/syntax-tree/AstReflection.hpp:29](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/syntax-tree/AstReflection.hpp#L29) — Métadonnées publiques de propriété limitées.
- [src/core/cursiv/syntax-tree/AstMemberAccess.hpp:42](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/syntax-tree/AstMemberAccess.hpp#L42) — Les valeurs et références sont exclues du visiteur de contenance.
- [src/core/cursiv/services/JsonValue.hpp:1](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/services/JsonValue.hpp#L1) — Conteneur JSON sans codec AST.

**Contexte API-001 — Sérialisation réversible d’un modèle AST.** Recherche des services de sérialisation et lecture des accès de champs : la création dynamique d’un nœud est disponible, mais elle ne suffit pas à remplir ses champs et rétablir les liens. Les noms de types sont consultables ; l’identité interne reste un tag C++ dont l’adresse ne peut servir de format persistant. Les string_view imposent aussi un stockage restauré stable. La représentation JSON actuelle ne définit aucune politique pour ces cas.

**Limite de preuve de ce périmètre.** L’absence concerne le contrat fourni par ce snapshot ; un codec applicatif reste réalisable avec les types concrets et ses propres règles d’ownership.

Comparaison incluse : [API-001](#comparison-api-001).

<a id="finding-api-entry-rule"></a>

### API-ENTRY-RULE — La règle d’entrée n’est pas sélectionnable par appel public

**P2 · écart d’API · confiance forte.** Périmètres : API-006.

**Mécanisme identifié.** Parser::parse reçoit seulement texte et token d’annulation. L’interface Grammar effacée ne propose pas getEntryRule malgré le commentaire résiduel de Parser.hpp. Le drive/ATN/table d’entrée est fixé dans le parseur de langage généré.

**Condition à vérifier.** Analyse d’expressions ou fragments via une sous-règle d’une grammaire de fichier.

**Impact.** Un client doit fournir une grammaire/instance de parseur dédiée ou une extension interne ; il ne peut porter directement un appel Langium parse(text,{rule:...}).

**Direction de correction.** Clarifier l’entrée dans le contrat public et, si cet usage est visé, générer des points d’entrée sélectionnables avec résultat typé et métadonnées CST correspondantes ; corriger le commentaire périmé.

**Sources à relire.**

- [src/core/cursiv/parser/Parser.hpp:23](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/parser/Parser.hpp#L23) — Commentaire getEntryRule et signature parse.
- [src/core/cursiv/grammar/Grammar.hpp:17](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/grammar/Grammar.hpp#L17) — Surface effectivement exposée.
- [src/engines/cursiv/ll/runtime/LlLanguageParser.hpp:52](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/ll/runtime/LlLanguageParser.hpp#L52) — Drive fixé à la construction.
- [src/engines/cursiv/lr/runtime/LrLanguageParser.hpp:39](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/LrLanguageParser.hpp#L39) — Drive fixé à la construction.

**Contexte API-006 — Choix d’une règle d’entrée pour un fragment.** L’API effacée stabilise le résultat entre moteurs mais ne porte pas d’identifiant de règle à invoquer. La sélection d’un root se fait au niveau de la déclaration/génération du langage. Reconfigurer le type de résultat C++ seul n’en fait pas une autre règle d’entrée ; les tables et leurs estampilles doivent correspondre.

**Limite de preuve de ce périmètre.** Le coût de génération de parseurs dédiés n’est pas mesuré. Aucune impossibilité algorithmique de multipoints d’entrée n’est affirmée.

Comparaison incluse : [API-006](#comparison-api-006).

<a id="finding-api-from-model"></a>

### API-FROM-MODEL — L’intégration d’un modèle synthétique exige une extension d’ownership

**P2 · écart d’API · confiance forte.** Périmètres : API-002.

**Mécanisme identifié.** DocumentFactory propose fromTextDocument/fromString/fromUri, tous orientés parsing. createDocument permet un document spécialisé ; AstReflection::createNode crée un nœud possédé. Toutefois ParseResult garde privés son arena et son registre de nœuds, et son pointeur value public ne transfère pas à lui seul leur propriété ni les références. Il manque une opération publique qui assemble ces éléments pour un AST construit sans texte.

**Condition à vérifier.** Import d’un format externe, transformation de modèles ou document synthétique sans reparsing.

**Impact.** Une extension doit gérer stockage des nœuds, vues textuelles, parents, références, état documentaire et comportement des services qui requièrent un CST ; simplement affecter value est insuffisant.

**Direction de correction.** Fournir un builder de résultat synthétique et DocumentFactory::fromModel, avec propriété explicite, état initial et capacités de navigation documentées en l’absence de CST.

**Sources à relire.**

- [src/core/cursiv/workspace/DocumentFactory.hpp:27](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DocumentFactory.hpp#L27) — Entrées textuelles et extension createDocument en ligne71.
- [src/core/cursiv/syntax-tree/ParseResult.hpp:31](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/syntax-tree/ParseResult.hpp#L31) — Arena et registre privés ; friend de test explicite.
- [src/core/cursiv/syntax-tree/AstReflection.hpp:54](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/syntax-tree/AstReflection.hpp#L54) — Factory de nœud isolé.

**Contexte API-002 — Intégration d’un AST synthétique dans un document.** La séparation factory/store permet d’ajouter un document spécialisé via Documents::addDocument. Ce point d’extension rend le cas réalisable, mais ne constitue pas un contrat d’intégration de modèle : le résultat normal est produit par AstReducer et son arena. Les services qui résolvent un nœud via nodes() ou une position via CST attendent davantage qu’un root non nul. Le modèle synthétique nécessite donc une convention et des services adaptés.

**Limite de preuve de ce périmètre.** Aucune impossibilité de construire un AST C++ n’est affirmée. Les garanties d’une extension utilisateur dépendent de son ownership et des services qu’elle remplace.

Comparaison incluse : [API-002](#comparison-api-002).

<a id="finding-api-generation-trace"></a>

### API-GENERATION-TRACE — Le texte généré ne conserve pas de carte de provenance

**P2 · écart d’API · confiance forte.** Périmètres : API-003.

**Mécanisme identifié.** TextBuilder écrit directement dans une string et retourne texte/taille/fragments. Il n’enregistre pas l’origine AST des octets, y compris après indentation ou nettoyage des blancs. SourceTranslation conserve la projection d’un texte transformé avant parsing vers le document d’entrée ; elle n’associe pas les régions de code généré à des nœuds/propriétés.

**Condition à vérifier.** Générateur qui doit relier une erreur ou une sélection dans le code produit au modèle DSL, éventuellement sur plusieurs fichiers.

**Impact.** Le client doit maintenir sa propre table de régions et la synchroniser avec les transformations de TextBuilder ; l’exemple domainmodel ne livre que les chaînes Java.

**Direction de correction.** Ajouter un résultat texte+traces et des scopes de provenance nœud/propriété/index, en calculant les positions après indentation et normalisation ; porter aussi l’URI du fichier source et cible.

**Sources à relire.**

- [src/core/cursiv/text/TextBuilder.hpp:62](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/text/TextBuilder.hpp#L62) — Composition immédiate sans origine.
- [src/core/cursiv/text/TextBuilder.hpp:98](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/text/TextBuilder.hpp#L98) — Extraction limitée au texte et sa taille.
- [src/core/cursiv/syntax-tree/SourceTranslation.hpp:15](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/syntax-tree/SourceTranslation.hpp#L15) — Autre direction de projection.
- [examples/domainmodel/src/cli/domainmodel/Generator.cpp:95](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/domainmodel/src/cli/domainmodel/Generator.cpp#L95) — Génération retournant une string.

**Contexte API-003 — Traçabilité du texte généré vers le modèle source.** La frontière a été suivie depuis le nœud Entity jusqu’au texte rendu : les noms et types deviennent des fragments sans marqueur source. size()/cut_from() pourraient aider un générateur spécifique à construire des régions, mais ni les URI, ni les propriétés, ni les régions imbriquées ne sont conservées par le framework. La normalisation des lignes rend une carte calculée avant émission insuffisante.

**Limite de preuve de ce périmètre.** Ce manque n’empêche pas la génération textuelle ; il concerne la navigation et les diagnostics ramenés depuis les fichiers produits.

Comparaison incluse : [API-003](#comparison-api-003).

<a id="finding-api-recovery-policy"></a>

### API-RECOVERY-POLICY — La politique de récupération n’est pas portable via Parser

**P2 · écart d’API · confiance forte.** Périmètres : API-007.

**Mécanisme identifié.** RepairBudget possède floor/perByte/perSearch/perParse et les constructeurs LlLanguageParser/LrLanguageParser l’acceptent. Le service Parser ne les expose ni à l’appel ni comme configuration commune ; il n’a pas de mode strict explicite. Mettre un budget à zéro borne une recherche mais ne constitue pas par contrat l’interdiction de toute stratégie de récupération.

**Condition à vérifier.** Application voulant choisir validation stricte, édition interactive et budgets par demande sans connaître le moteur concret.

**Impact.** La configuration requiert le type moteur/instanciation ; create_parser() généré ne reçoit pas de paramètres. accepted, diagnostics et DiagnosticKind::bounded exposent déjà des issues, dont la coupure par budget. Ils ne constituent pas une politique publique de dépense ou un mode strict par appel commun aux moteurs.

**Direction de correction.** Définir des options stables de politique stricte/réparation et raccorder explicitement les budgets LL/LR à la factory publique ; conserver et préciser les issues existantes, dont bounded, plutôt que les réinventer.

**Sources à relire.**

- [src/core/cursiv/parser/Parser.hpp:31](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/parser/Parser.hpp#L31) — Signature commune sans politique.
- [src/engines/cursiv/shared/runtime/RepairBudget.hpp:74](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/shared/runtime/RepairBudget.hpp#L74) — Bornes et structure de configuration.
- [src/engines/cursiv/ll/runtime/LlLanguageParser.hpp:52](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/ll/runtime/LlLanguageParser.hpp#L52) — Budget configurable côté LL.
- [src/engines/cursiv/lr/runtime/LrLanguageParser.hpp:39](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/LrLanguageParser.hpp#L39) — Budget configurable côté LR.
- [src/core/cursiv/syntax-tree/ParseResult.hpp:78](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/syntax-tree/ParseResult.hpp#L78) — Diagnostic/erreur/accepted observables.
- [src/core/cursiv/syntax-tree/ParseDiagnostic.hpp:29](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/syntax-tree/ParseDiagnostic.hpp#L29) — Le diagnostic bounded existe déjà.
- [src/engines/cursiv/shared/gen/CommonArtifacts.cpp:88](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/shared/gen/CommonArtifacts.cpp#L88) — Factory générée sans option.
- [src/engines/cursiv/shared/gen/CommonArtifacts.cpp:120](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/shared/gen/CommonArtifacts.cpp#L120) — Construction avec budget moteur par défaut.

**Contexte API-007 — Contrôle public de la récupération d’erreurs.** Les contrôles existants ont été suivis jusqu’aux deux constructeurs moteurs pour éviter une fausse absence : Cursiv possède un budget de travail saturant partagé et des indicateurs de résultat. La limite est sa présentation aux consommateurs de services et l’absence de contrat strict commun. La justesse de chaque décompte/coupure est couverte séparément par COM, LL et LR. DiagnosticKind::bounded identifie déjà les spans arrêtés par la borne de réparation ; l’audit ne conclut pas à une coupure entièrement silencieuse.

**Limite de preuve de ce périmètre.** Ni le budget Cursiv ni la configuration Chevrotain ne constituent une garantie temporelle murale. Aucune exécution de parsing invalide.

Comparaison incluse : [API-007](#comparison-api-007).

<a id="finding-ass-ast-optional-sentinel"></a>

### ASS-AST-OPTIONAL-SENTINEL — Une valeur optionnelle absente est indiscernable du texte '(unset)'

**P2 · défaut · confiance forte.** Périmètres : ASS-007.

**Mécanisme identifié.** yield_field rend un optional absent avec ValueKind::text et la chaîne '(unset)'. Un optional<string> présent contenant exactement cette chaîne est rendu de la même façon, avec le même nom et index. AstCompare compare ces textes et conclut à l’égalité.

**Condition à vérifier.** Un membre optionnel textuel attendu vaut '(unset)' mais le parseur l’a laissé absent, ou l’inverse.

**Impact.** Une perte d’affectation AST peut être masquée par l’oracle différentiel.

**Direction de correction.** Encoder la présence dans un discriminant distinct ou comparer les valeurs typées ; ne pas utiliser une chaîne utilisateur comme sentinelle d’absence.

**Sources à relire.**

- [src/fuzz/cursiv/CursivActions.hpp:647](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/fuzz/cursiv/CursivActions.hpp#L647) — absence encodée comme texte
- [src/fuzz/cursiv/CursivActions.hpp:43](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/fuzz/cursiv/CursivActions.hpp#L43) — string rendu tel quel
- [src/fuzz/cursiv/AstCompare.hpp:183](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/fuzz/cursiv/AstCompare.hpp#L183) — égalité sur le texte

**Contexte ASS-007 — Comparateur AST, lecteurs de valeurs et exclusions.** AstCompare compare les identités de type, listes de valeurs rendues puis enfants visités avec nom/index et chemin d’erreur. Le parcours est itératif. Les membres excusés du modèle sont ôtés des deux côtés ; unreadable est enregistré dans blind. Les variantes scalaires portent leur index, les variantes de nœuds restent aveugles. Les références simples sont observées par refText, sans résolution. CursivActions réutilise converters et écritures de production : l’oracle est indépendant du parsing mais ne teste pas ces conversions de façon indépendante. ast_of ne produit que la forme et spans_of les spans des nœuds, sans les valeurs.

**Limite de preuve de ce périmètre.** Les flottants sont tous convertis en double avant rendu : la précision supplémentaire de long double et les payloads NaN ne sont pas observés. Les membres aveugles sont rapportés, pas une preuve d’accord sémantique complet. Analyse statique.

<a id="finding-ass-codeaction-uri-effacee"></a>

### ASS-CODEACTION-URI-EFFACEE — Le helper applique au document courant les edits de toutes les URI

**P2 · défaut · confiance forte.** Périmètres : ASS-005.

**Mécanisme identifié.** testCodeAction parcourt action.edit->changes sans utiliser la clé uri, concatène tous les edits et les applique au TextDocument du diagnostic. Une action qui vise par erreur un autre document peut donc produire localement le texte attendu, puis passer la revalidation.

**Condition à vérifier.** Le provider retourne une clé URI incorrecte ou une action réellement multidocument.

**Impact.** L’oracle peut valider un correctif qui ne modifierait pas le bon fichier chez le client, ou rejeter une action multidocument valide en mélangeant ses edits.

**Direction de correction.** Vérifier l’URI pour les cas monodocument et appliquer les edits sur chaque document nommé pour les cas multidocuments ; traiter explicitement documentChanges.

**Sources à relire.**

- [src/test/cursiv/CodeActionResult.cpp:79](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/test/cursiv/CodeActionResult.cpp#L79) — URI ignorée
- [src/test/cursiv/CodeActionResult.cpp:85](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/test/cursiv/CodeActionResult.cpp#L85) — application à un texte unique
- [src/test/cursiv/CodeActionResult.cpp:88](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/test/cursiv/CodeActionResult.cpp#L88) — revalidation de ce texte

**Contexte ASS-005 — Attentes de formatting, renommage et actions de code.** Formatting vérifie le texte résultant après applyEdits en normalisant CRLF vers LF ; il ne prouve ni minimalité des edits ni préservation exacte des EOL. Rename exige un résultat optionnel selon le cas, trie les edits du document courant puis compare leur nombre, plage et newText ; il ne vérifie pas les edits des autres documents ni documentChanges. CodeAction demande un diagnostic du code voulu, une action, un résultat textuel et la disparition de ce code après revalidation. Son regroupement d’edits efface cependant l’identité des documents.

**Limite de preuve de ce périmètre.** Analyse statique uniquement ; aucun test exécuté.

<a id="finding-ass-cst-sousarbre-traverse"></a>

### ASS-CST-SOUSARBRE-TRAVERSE — L’oracle CST du fuzzer accepte un enfant débordant du sous-arbre parent

**P2 · défaut · confiance forte.** Périmètres : ASS-006.

**Mécanisme identifié.** Le parcours des enfants fait k=subtree_begin(child) puis termine quand k<=low, sans exiger k>=low ni k==low à la sortie. Les contrôles individuels prouvent seulement que chaque sous-arbre ne sort pas du début de la tape. Trois records de subtreeSize1,2,2, tous sur le même span[0,1), satisfont ces contrôles alors que le dernier parent prétend ne contenir que le record1, dont le sous-arbre commence au record0.

**Condition à vérifier.** Un producteur émet une taille de sous-arbre parent trop petite mais individuellement plausible.

**Impact.** La campagne FuzzRun peut compter une tape structurellement incohérente comme valide ; le CstAudit distinct possède déjà un contrôle qui détecterait ce cas.

**Direction de correction.** Exiger que chaque enfant tienne dans l’étendue restante et que la chaîne termine exactement à low ; partager les invariants structurels entre les deux oracles.

**Sources à relire.**

- [src/fuzz/cursiv/CstInvariants.hpp:31](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/fuzz/cursiv/CstInvariants.hpp#L31) — borne globale seulement
- [src/fuzz/cursiv/CstInvariants.hpp:41](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/fuzz/cursiv/CstInvariants.hpp#L41) — parcours sans borne locale
- [src/fuzz/cursiv/CstInvariants.hpp:49](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/fuzz/cursiv/CstInvariants.hpp#L49) — saut pouvant dépasser low
- [tests/engines/shared/CstAudit.hpp:50](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/tests/engines/shared/CstAudit.hpp#L50) — contrôle local disponible
- [src/fuzz/cursiv/FuzzRun.hpp:127](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/fuzz/cursiv/FuzzRun.hpp#L127) — oracle utilisé en campagne

**Contexte ASS-006 — Oracle CST et rôle des empreintes de tape.** CstInvariants contrôle chaque span, la taille globale de sous-arbre et l’ordre/containment des spans enfants sans lire les tables moteur. CstAudit ajoute monotonie globale, bornes de chaîne enfants, duplicatas de trivia et présence/rattachement d’une racine et d’un AST pour un document accepté. Son commentaire de pavage ne signifie pas couverture sans trous : le contrôle des feuilles exclut le recouvrement mais autorise les gaps. TapeSeal est une empreinte64bits des records, de leurs ids et flags, pas une preuve d’égalité ni un encodage du texte ou de l’AST.

**Limite de preuve de ce périmètre.** Analyse statique uniquement ; aucun test exécuté.

<a id="finding-ass-diagnostic-anterieur"></a>

### ASS-DIAGNOSTIC-ANTERIEUR — La session LSP accepte un diagnostic antérieur à l’ouverture qu’elle observe

**P2 · défaut · confiance forte.** Périmètres : ASS-020.

**Mécanisme identifié.** waitFor cherche findLast dans toute la liste received, sans curseur de début ni version. Après le scan du workspace, didOpen attend seulement une URI déjà présente ; l’attente peut donc se résoudre immédiatement avec le snapshot du scan. Le récapitulatif dit après la totalité des ouvertures réutilise de même le dernier snapshot déjà reçu, sans barrière garantissant le traitement des ouvertures.

**Condition à vérifier.** Le serveur a déjà publié un diagnostic de la même URI, notamment lors du scan initial d’un workspace.

**Impact.** Le client peut annoncer un snapshot après ouverture alors qu’aucune publication nouvelle n’a été observée ; une régression des notifications didOpen peut être masquée et les sorties imprimées peuvent être anciennes.

**Direction de correction.** Mémoriser une frontière de réception avant l’envoi et attendre une publication ultérieure, corrélée à la version lorsque disponible ; utiliser une barrière explicite avant le récapitulatif final.

**Sources à relire.**

- [tools/lsp-session.mjs:151](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/tools/lsp-session.mjs#L151) — findLast sur historique entier
- [tools/lsp-session.mjs:238](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/tools/lsp-session.mjs#L238) — publication du scan consommée sans retrait
- [tools/lsp-session.mjs:259](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/tools/lsp-session.mjs#L259) — attente didOpen filtrée par URI seulement
- [tools/lsp-session.mjs:270](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/tools/lsp-session.mjs#L270) — snapshot final sans causalité

**Contexte ASS-020 — Sessions LSP/MCP : cadrage, corrélation et assertions d’ordre.** Les transports LSP travaillent sur Buffer et Content-Length en octets ; le client MCP stdio emploie StringDecoder pour préserver UTF-8 entre fragments. Les requêtes sont corrélées par id. LSP-order vérifie exactement Beta,Gamma après didChange sans attente intermédiaire et constitue une assertion d’ordre réelle pour ce scénario. MCP-tunnel exige que le texte du diagnostic mentionne le document non sauvegardé et son type inexistant. Le client LSP général affiche la plupart des résultats métier sans oracle d’égalité ; son contrat se limite au handshake et aux snapshots. MCP-session distingue erreur JSON-RPC et result.isError des appels. La gestion de fin de processus et la fraîcheur des snapshots restent imparfaites.

**Limite de preuve de ce périmètre.** Analyse statique uniquement. Les clients sont des sondes du dépôt, pas une preuve exhaustive LSP/MCP. LSP-session attend la fin sans timeout, contrairement aux trois autres. Les URI de deux petits clients sont concaténées sans encodage ; le tunnel vérifie des sous-chaînes plutôt qu’un diagnostic structuré. Aucune session exécutée.

<a id="finding-ass-liste-cardinalite-effacee"></a>

### ASS-LISTE-CARDINALITE-EFFACEE — La comparaison peut accepter des listes de cardinalités différentes

**P2 · défaut · confiance forte.** Périmètres : ASS-003.

**Mécanisme identifié.** Quand les tailles diffèrent, expectCompletion et expectSymbols ne comparent pas les nombres : ils comparent les chaînes joined séparées par ', '. Ainsi ['a, b'] et ['a','b'] produisent toutes deux 'a, b'. Le message mentionne la taille différente mais expectedFunction juge uniquement l’égalité des deux chaînes ; le hook par défaut et count_expectations ne signalent alors aucun échec. Des chaînes vides créent aussi des collisions.

**Condition à vérifier.** Un nom/label ou une projection personnalisée contient le séparateur, ou des valeurs vides, et le nombre de résultats est incorrect.

**Impact.** Un résultat incomplet ou surnuméraire peut rendre l’attente verte.

**Direction de correction.** Comparer systématiquement les tailles indépendamment, puis chaque valeur, ou utiliser une sérialisation injective du tableau. Mutualiser cette logique dupliquée.

**Sources à relire.**

- [src/test/cursiv/ExpectedCompletion.cpp:30](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/test/cursiv/ExpectedCompletion.cpp#L30) — jointure ambiguë
- [src/test/cursiv/ExpectedCompletion.cpp:92](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/test/cursiv/ExpectedCompletion.cpp#L92) — branche tailles différentes
- [src/test/cursiv/ExpectedSymbols.cpp:67](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/test/cursiv/ExpectedSymbols.cpp#L67) — même mécanisme
- [src/test/cursiv/ExpectFunction.cpp:32](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/test/cursiv/ExpectFunction.cpp#L32) — égalité décisive
- [src/test/cursiv/Check.cpp:17](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/test/cursiv/Check.cpp#L17) — hook ignore message si chaînes égales

**Contexte ASS-003 — Projection et comparaison des résultats de complétion et de symboles.** Les attentes simples de complétion projettent les items sur label ou itemToString et les trient stablement par sortText, absent remplacé par la chaîne0. Les symboles sont projetés seulement au premier niveau sur name ou symbolToString, dans l’ordre reçu. Les variantes callback reçoivent le résultat entier et permettent une assertion plus forte. Les plages, enfants, edits, isIncomplete et métadonnées restent hors de la comparaison par défaut ; le fallback de tri0 ne reproduit pas un classement par label.

**Limite de preuve de ce périmètre.** Analyse statique uniquement ; aucun test exécuté.

<a id="finding-ass-rgram-campagne-vide"></a>

### ASS-RGRAM-CAMPAGNE-VIDE — Une campagne Rgram vide ou sans aucun arbre jugé peut réussir

**P2 · limite · confiance forte.** Périmètres : ASS-011.

**Mécanisme identifié.** rgram_lane affiche docs.size(), inLanguage et trees mais son code de sortie ne dépend que de bad. documents() peut rendre moins de cas que demandé après ses retries et aucun seuil minimal n’est imposé ; aucune réussite positive ou comparaison AST n’est exigée pour une famille AST. L’appel avec documents=0 rend immédiatement une campagne verte.

**Condition à vérifier.** Aucun document produit, corpus entièrement refusé par les deux côtés, ou arbres de référence tous ambigus/incomplets.

**Impact.** Un succès peut être interprété comme assurance fonctionnelle alors que le langage accepté ou les AST n’ont pas été effectivement vérifiés.

**Direction de correction.** Imposer au moins un cas produit et des portes explicites de couverture positive/AST adaptées à chaque famille ; séparer succès, absence de couverture et dépassement d’oracle.

**Sources à relire.**

- [tests/oracle/RgramCfg.cpp:217](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/tests/oracle/RgramCfg.cpp#L217) — essais bornés sans minimum
- [tests/oracle/RgramCfg.cpp:391](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/tests/oracle/RgramCfg.cpp#L391) — arbre absent sur cap/ambiguïté
- [tests/oracle/rgram_lane.cpp:122](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/tests/oracle/rgram_lane.cpp#L122) — comptes affichés
- [tests/oracle/rgram_lane.cpp:126](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/tests/oracle/rgram_lane.cpp#L126) — seul bad décide du succès

**Contexte ASS-011 — Reconnaisseur de référence des grammaires aléatoires.** RgramCfg abaisse le sous-langage en CFG avec symboles de conditions et contextes tight, puis reconnaît par ensembles d’items de type Earley. L’achèvement nullable est repris jusqu’au point fixe. Le corpus mélange dérivations et mutations et garde des textes uniques avec20 essais par cible. Pour l’AST, l’oracle ne donne une chaîne que si les arbres projetés sont uniques ; cycles et dépassement du cap8 suppriment cette réponse. La comparaison runtime utilise accepted+absence de diagnostics, exige un AST sur un parse propre et compare seulement la forme. Le watchdog10s encadre le parseur, pas le calcul CFG de référence.

**Limite de preuve de ce périmètre.** Un arbre vide fusionne absence de demande, ambiguïté, cycle et cap ; la synthèse ne sépare pas leurs nombres. Les bornes internes des clés d’items sont larges pour ce petit générateur, pas une garantie pour une CFG arbitraire. Aucun échec runtime prétendu.

<a id="finding-ass-session-arret-ignore"></a>

### ASS-SESSION-ARRET-IGNORE — Trois clients de session peuvent réussir après arrêt forcé ou sortie en erreur du serveur

**P2 · défaut · confiance forte.** Périmètres : ASS-020.

**Mécanisme identifié.** lsp-order-session, mcp-session et mcp-tunnel-session résolvent leur attente de fin à l’événement exit/close sans examiner code ou signal ; leur timer tue le serveur après deux secondes sans positionner failed. Lorsque les assertions précédentes ont réussi, un serveur qui répond au shutdown puis reste vivant, ou qui sort avec un code non nul, donne donc un client qui retourne 0.

**Condition à vérifier.** Le scénario métier aboutit, puis le serveur refuse de terminer ou termine anormalement.

**Impact.** Une régression du cycle de vie reste invisible au verdict de session ; le résultat peut être vert malgré une terminaison forcée.

**Direction de correction.** Faire échouer le scénario sur expiration du délai, signal inattendu ou code non nul ; traiter aussi un processus déjà terminé avant l’installation du listener.

**Sources à relire.**

- [tools/lsp-order-session.mjs:88](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/tools/lsp-order-session.mjs#L88) — kill/exit sans mise à jour failed
- [tools/lsp-order-session.mjs:92](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/tools/lsp-order-session.mjs#L92) — code final dépend uniquement de failed
- [tools/mcp-session.mjs:112](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/tools/mcp-session.mjs#L112) — close et timer ignorent cause de fin
- [tools/mcp-tunnel-session.mjs:113](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/tools/mcp-tunnel-session.mjs#L113) — même séquence de fin

**Contexte ASS-020 — Sessions LSP/MCP : cadrage, corrélation et assertions d’ordre.** Les transports LSP travaillent sur Buffer et Content-Length en octets ; le client MCP stdio emploie StringDecoder pour préserver UTF-8 entre fragments. Les requêtes sont corrélées par id. LSP-order vérifie exactement Beta,Gamma après didChange sans attente intermédiaire et constitue une assertion d’ordre réelle pour ce scénario. MCP-tunnel exige que le texte du diagnostic mentionne le document non sauvegardé et son type inexistant. Le client LSP général affiche la plupart des résultats métier sans oracle d’égalité ; son contrat se limite au handshake et aux snapshots. MCP-session distingue erreur JSON-RPC et result.isError des appels. La gestion de fin de processus et la fraîcheur des snapshots restent imparfaites.

**Limite de preuve de ce périmètre.** Analyse statique uniquement. Les clients sont des sondes du dépôt, pas une preuve exhaustive LSP/MCP. LSP-session attend la fin sans timeout, contrairement aux trois autres. Les URI de deux petits clients sont concaténées sans encodage ; le tunnel vérifie des sous-chaînes plutôt qu’un diagnostic structuré. Aucune session exécutée.

<a id="finding-bld-distribution-anciens-modules"></a>

### BLD-DISTRIBUTION-ANCIENS-MODULES — Une sortie de distribution réutilisée peut embarquer un module absent du build courant

**P2 · défaut · confiance forte.** Périmètres : BLD-010.

**Mécanisme identifié.** wasm-dist conserve le répertoire destination et ne recopie que les binaires présents ; son succès n’exige qu’un seul serveur. package-vsix utilise ensuite les fichiers déjà présents sous out/modules et vérifie uniquement leur existence. Une ancienne copie du serveur B reste donc disponible quand le build courant contient seulement A ; le paquet B peut être recréé avec l’ancien module et les notices actuelles.

**Condition à vérifier.** Réutilisation du même dossier de sortie après un build partiel ou un changement de configuration qui ne produit plus certains serveurs.

**Impact.** Des VSIX de la même exécution peuvent mêler des révisions/configurations distinctes sans erreur de packaging.

**Direction de correction.** Collecter dans un staging neuf puis publier atomiquement, ou supprimer/invalider explicitement les modules non produits et exiger la liste complète attendue avant empaquetage.

**Sources à relire.**

- [tools/wasm-dist.sh:11](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/tools/wasm-dist.sh#L11) — destination conservée
- [tools/wasm-dist.sh:21](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/tools/wasm-dist.sh#L21) — binaires absents ignorés
- [tools/wasm-dist.sh:28](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/tools/wasm-dist.sh#L28) — un seul module suffit
- [tools/package-vsix.sh:18](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/tools/package-vsix.sh#L18) — réutilise out/modules
- [tools/package-vsix.sh:26](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/tools/package-vsix.sh#L26) — existence seule du module

**Contexte BLD-010 — Collecte des modules Wasm et notices.** cursiv_package_language ne prépare les notices et la copie d’extension que pour un cross-build threaded, une target LSP existante et un dossier vscode. Les notices Cursiv/lsp-framework sont prises aux sources réellement résolues ; mimalloc est la notice embarquée du patch. wasm-dist exige les trois notices et au moins un serveur puis copie les modules construits sous examples/*/src. Le bundle Node complet est assemblé par package-vsix ; wasm-dist est une collection pour la démo et ne prétend pas démarrer seule les modules.

**Limite de preuve de ce périmètre.** Analyse statique uniquement ; aucune configuration CMake, compilation ni exécution. Le script utilise stat -c (GNU) et les chemins simples du build Ninja ; sa portabilité à macOS/multiconfig n’est pas promise ni exécutée.

<a id="finding-bld-package-rafraichissement"></a>

### BLD-PACKAGE-RAFRAICHISSEMENT — Une modification isolée du runner ou une copie supprimée ne rafraîchit pas vscode/server

**P2 · défaut · confiance forte.** Périmètres : BLD-010.

**Mécanisme identifié.** Les copies du module, du runner et des licences sont des commandes POST_BUILD du binaire LSP. Aucun output de copie ni dépendance du binaire vers wasi-run.mjs/notices n’est déclaré. Si le binaire est déjà à jour, modifier uniquement le runner ou supprimer un fichier de vscode/server ne déclenche pas cette commande.

**Condition à vérifier.** Build incrémental après modification du runner/notices, ou après suppression d’un artefact empaqueté sans changement du binaire.

**Impact.** Le dossier utilisé par npm run package peut contenir un ancien runner ou manquer de fichiers malgré un build réussi. F5 du scaffold utilise aussi ce runner copié ; F5 des exemples recherche normalement tools/wasi-run.mjs dans le dépôt et ne présente donc pas cette même dépendance.

**Direction de correction.** Déclarer une cible de staging avec fichiers de sortie et dépendances explicites, ou une cible de copie systématique dépendant du serveur.

**Sources à relire.**

- [cmake/CursivGrammar.cmake:114](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/cmake/CursivGrammar.cmake#L114) — POST_BUILD attaché au lien uniquement
- [cmake/CursivGrammar.cmake:118](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/cmake/CursivGrammar.cmake#L118) — runner copié sans dépendance
- [tools/prepare.sh:19](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/tools/prepare.sh#L19) — préparation ne demande que le build LSP

**Contexte BLD-010 — Collecte des modules Wasm et notices.** cursiv_package_language ne prépare les notices et la copie d’extension que pour un cross-build threaded, une target LSP existante et un dossier vscode. Les notices Cursiv/lsp-framework sont prises aux sources réellement résolues ; mimalloc est la notice embarquée du patch. wasm-dist exige les trois notices et au moins un serveur puis copie les modules construits sous examples/*/src. Le bundle Node complet est assemblé par package-vsix ; wasm-dist est une collection pour la démo et ne prétend pas démarrer seule les modules.

**Limite de preuve de ce périmètre.** Analyse statique uniquement ; aucune configuration CMake, compilation ni exécution. Le script utilise stat -c (GNU) et les chemins simples du build Ninja ; sa portabilité à macOS/multiconfig n’est pas promise ni exécutée.

<a id="finding-bld-scaffold-ext-nonbornee"></a>

### BLD-SCAFFOLD-EXT-NONBORNEE — EXT n’est ni échappée ni limitée à un suffixe de fichier

**P2 · défaut · confiance forte.** Périmètres : BLD-014.

**Mécanisme identifié.** La seule validation d’EXT impose un point initial. Sa valeur est ensuite substituée telle quelle dans le JSON, le CMake et le chemin example/hello<EXT>. Par exemple .bad" produit une chaîne JSON mal fermée. Des séparateurs et segments .. sont également acceptés ; .x/../../../outside forme un chemin qui sort lexicalement de DIR, alors que la vérification de destination non vide ne porte que sur DIR.

**Condition à vérifier.** Le paramètre EXT contient un guillemet, une barre de chemin, un retour à la ligne ou une autre séquence impropre à un suffixe simple.

**Impact.** Le scaffolder peut annoncer un projet créé mais invalide ; une extension contenant des traversées peut écrire l’exemple au-delà du répertoire contrôlé.

**Direction de correction.** Valider EXT contre une grammaire explicite de suffixe sans séparateurs/contrôles et échapper ses représentations JSON/CMake ; normaliser chaque sortie et vérifier qu’elle reste sous DIR avant toute écriture.

**Sources à relire.**

- [cmake/cursiv-new.cmake.in:38](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/cmake/cursiv-new.cmake.in#L38) — seul test du point
- [cmake/cursiv-new.cmake.in:59](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/cmake/cursiv-new.cmake.in#L59) — substitution brute du chemin et contenu
- [cmake/cursiv-new.cmake.in:61](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/cmake/cursiv-new.cmake.in#L61) — écriture sans contrôle de confinement
- [templates/project/vscode/package.json:27](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/templates/project/vscode/package.json#L27) — interpolation dans chaîne JSON
- [docs/cursiv-new.cmake:339](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/docs/cursiv-new.cmake#L339) — EXT dans chemin de fichier

**Contexte BLD-014 — Production et paramétrage du scaffolder.** Le packer lit les templates et les quatre ressources partagées puis les embarque comme arguments bracket-quoted avant le footer. La vérification textuelle statique a trouvé les 36 émissions identiques à leurs fichiers source et le préambule identique au template avec tag main ; aucun générateur exécuté pour ce contrôle. NAME est validé comme identifiant C++ et son lowercase est contrôlé contre mots réservés, underscore initial et double underscore. La destination est refusée si non vide, les chemins et contenus sont substitués en @ONLY. Le test d’intégration existant relu compare également l’artefact généré aux sources et couvre des noms réservés.

**Limite de preuve de ce périmètre.** Analyse statique uniquement ; aucun build, test, générateur, packaging ou workflow exécuté. CURSIV_TAG vaut main par défaut : un projet nouveau suit cette référence mobile jusqu’à pin explicite. Les délimiteurs bracket fixes nécessitent d’éviter leur séquence de fermeture dans un futur template.

<a id="finding-com-election-hors-quota"></a>

### COM-ELECTION-HORS-QUOTA — L’élection finale commune poursuit des parcours hors quota et sans annulation

**P2 · limite · confiance forte.** Périmètres : COM-006, LR-058, LR-059.

**Mécanisme identifié.** repair_elect regroupe et compare tous les goals, parcourt leurs histoires via Fold/Same/GestureRank puis collecte les alternatives sans débit de travail ni consultation d’annulation. Les deux moteurs l’appellent après une recherche éventuellement épuisée.

**Condition à vérifier.** Nombreux goals de coût comparable, histoires profondes ou grand volume d’alternatives, notamment quand le quota de recherche vient d’être atteint.

**Impact.** Le quota des étapes de reconnaissance ne borne pas la latence totale de réparation ; une annulation peut attendre la fin du regroupement et de la collecte.

**Direction de correction.** Faire participer regroupement, hash exact, comparaisons et collecte au budget commun, avec des points d’annulation et une politique explicite de résultat partiel ; réutiliser des résumés d’histoires sans remplacer l’égalité exacte par un hash.

**Sources à relire.**

- [src/engines/cursiv/shared/runtime/RepairSearch.hpp:178](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/shared/runtime/RepairSearch.hpp#L178) — Callbacks de hash/égalité et regroupement de tous les goals.
- [src/engines/cursiv/shared/runtime/RepairSearch.hpp:200](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/shared/runtime/RepairSearch.hpp#L200) — Comparaison puis collecte intégrale.
- [src/engines/cursiv/lr/runtime/lr_search.hpp:1037](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/lr_search.hpp#L1037) — Pas de retour anticipé sur exhausted avant l’élection.

**Contexte COM-006 — Élection commune entre groupes de buts.** Le regroupement emploie un hash Fold et une égalité Same, puis attribue à chaque groupe une portée partagée. L’élection compare portée décroissante, clé croissante et rang, et collecte uniquement les alternatives à même portée/clé et, en LR, même racine. Les callers vérifient qu’il existe des goals avant l’appel. En revanche, cette phase s’exécute encore après arrêt de la recherche au quota : construction de la hash-map, calculs répétés de Fold/Same, comparaisons de rang et collecte des insertions ne consultent ni work/limit ni un token d’annulation. Le callback race borne sa propre simulation, pas ces parcours communs. L’ancien constat de budget incomplet de l’élection est donc revalidé au niveau qui implémente réellement cette partie.

**Limite de preuve de ce périmètre.** Le coût non compté est certain à la lecture. Son amplitude dépend du nombre de goals et de la profondeur des histoires ; aucune latence ni borne pratique mesurée.

**Contexte LR-058 — Recherche best-first et allocation du travail.** Le tas ordonne la borne/coût puis perte/gestes/fautes/inventions, et ignore les configurations gone. boundAfter déclenche le calcul de la borne sans modifier les coûts ; une borne relevée remet l’état en queue avant expansion. Les buts et furthest distinguent solution validée et repli. Les charges couvrent essais/forks, scans, cactus et gaps, avec les exceptions LR-DP-TRAVAIL-NON-FACTURE et COM-ELECTION-HORS-QUOTA. restart conserve le travail/budget cumulé du document et les capacités de nombreux buffers, tout en réinitialisant la recherche. Aucune optimalité globale au-delà du domaine/quota et des réserves relevées n’est déduite de ce code.

**Limite de preuve de ce périmètre.** Analyse statique uniquement ; aucun scénario exécuté ni coût mesuré.

**Contexte LR-059 — Regroupement et course entre buts.** Fold présélectionne les gestes, puis same compare les champs exacts et n’ignore que les choix de lecture non épinglés prévus par le contrat. La racine participe au fold et les sources distinctes ne sont pas fusionnées arbitrairement. La course conserve des ensembles de groupes triés, les unit quand les futurs se rencontrent et recopie la provenance aux forks. steps compte les progressions de tokens ; work borne les opérations de lecture. Le seuil de course donne un horizon commun, pas une preuve d’ordre lexical. Le regroupement initial sans quota est déjà COM-ELECTION-HORS-QUOTA.

**Limite de preuve de ce périmètre.** Analyse statique uniquement ; aucun scénario exécuté ni coût mesuré.

<a id="finding-com-typo-incomplet"></a>

### COM-TYPO-INCOMPLET — Le plafond de branchement typo n’est pas propagé comme une exploration incomplète

**P2 · défaut · confiance forte.** Périmètres : COM-012, LL-038, LR-056, LR-057.

**Mécanisme identifié.** Lors d’un désaccord lexical, les branches d’avance fantôme et de substitution de typo_each sont limitées aux quatre premiers successeurs du DFA. Au-delà, le noyau appelle refused(), mais LlTypoTape ignore ce signal et LrTypoTape ne fait qu’incrémenter un compteur : TypoBounds::shortOf, relié à cut, reste inchangé. Les mémos LL et LR utilisent pourtant cut pour distinguer une exploration complète d’une exploration tronquée. lr_search_misread perd également ce signal : son compteur local stops n’est pas relu avant mémorisation, puis le résultat alimente la borne LR calculée par programmation dynamique.

**Condition à vérifier.** La recherche atteint la branche de désaccord dans un état comportant plus de quatre successeurs, et aucune autre limite ne positionne déjà cut.

**Impact.** Des candidats de réparation peuvent être omis selon l’ordre des successeurs DFA ; le résultat tronqué peut être mémorisé comme complet. Côté LR, le compteur stops de lr_search_misread n’est pas propagé à boundShort avant consommation par la borne DP. La perte du statut d’incomplétude est établie ; aucune dérivation concrète montrant un élagage incorrect ou une réparation finale sous-optimale n’est affirmée.

**Direction de correction.** Propager tout abandon de branche vers un statut explicite d’incomplétude conservé par les adaptateurs ; empêcher la mémorisation comme résultat complet et appliquer le repli conservateur de la borne. Si shortOf est réutilisé, le positionner pour cette troncature même lorsque l’horizon atteint la fin du document.

**Sources à relire.**

- [src/engines/cursiv/shared/runtime/TypoWalk.hpp:280](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/shared/runtime/TypoWalk.hpp#L280) — Signal suivi d’une exploration tronquée.
- [src/engines/cursiv/ll/runtime/ll_rescan.hpp:77](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/ll/runtime/ll_rescan.hpp#L77) — Le signal est perdu.
- [src/engines/cursiv/lr/runtime/lr_rescan.hpp:75](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/lr_rescan.hpp#L75) — Le signal n’atteint que les statistiques.
- [src/engines/cursiv/lr/runtime/lr_search_bound.hpp:268](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/lr_search_bound.hpp#L268) — Publication dans le cache si cut est faux.
- [src/engines/cursiv/lr/runtime/lr_search_bound.hpp:284](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/lr_search_bound.hpp#L284) — misread ignore aussi stops et peut alimenter la borne comme résultat complet.
- [src/engines/cursiv/ll/runtime/ll_search.hpp:934](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/ll/runtime/ll_search.hpp#L934) — seul cut empêche la mémoïsation
- [src/engines/cursiv/lr/runtime/lr_search_bound.hpp:515](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/lr_search_bound.hpp#L515) — Résultat misread consommé par la DP.
- [src/engines/cursiv/lr/runtime/lr_search_bound.hpp:663](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/lr_search_bound.hpp#L663) — Repli vers la borne triviale seulement lorsque boundShort est vrai.

**Contexte COM-012 — Bornes du parcours typo et statut incomplet.** TypoBounds distingue bien horizon et fin documentaire : cut positionne shortOf uniquement quand extent<docLen, spent le positionne après dépassement de most, et le noyau consulte ces deux voies. La borne kTypoEdges utilise toutefois un troisième chemin : elle appelle refused puis abandonne les successeurs au-delà des quatre premiers. Les adaptateurs réels ne traduisent pas ce refus en shortOf : LL l’ignore et LR ne fait qu’incrémenter searchStops. Le booléen cut peut donc rester faux, alors même que des chemins ont été abandonnés, et les deux callers mettent ensuite ces candidats en cache comme un résultat complet. Le contrat explicite du noyau exige précisément que refused ne soit pas perdu.

**Limite de preuve de ce périmètre.** La perte du signal est démontrée statiquement pour un état présentant plus de quatre successeurs ; aucune grammaire ni latence n’a été exécutée. L’effet sur le choix final dépend des alternatives abandonnées. La perte du signal et son chemin jusqu’aux mémos et à la borne sont établis. Aucun cas démontrant qu’une branche omise conduit à une réparation finale utile, ni un élagage incorrect concret, n’est affirmé.

**Contexte LL-038 — Réinterprétation lexicale et fautes de mot.** Les tokens ouverts sont relus dans le mode propre au candidat et bornés par section/horizon ; une lecture coupée n’entre pas dans le mémo. Les fautes de mot sont indexées par position, modes et ensembles exacts et respectent réservations/frontières. Une exception subsiste : le plafond interne des successeurs de TypoWalk n’est pas propagé au statut cut.

**Limite de preuve de ce périmètre.** Constat commun avec l’audit LR/COM ; compter une seule fois la clé COM-TYPO-INCOMPLET.

**Contexte LR-056 — Relecture lexicale et caches du réparateur.** Les clés sont lexed=(position,mode,inside), typos=(état,position), readings/ends=(position,mode). written ne mémoïse par position seule qu’avec un mode d’erreur global. Les lectures atteignant leur borne ne sont pas publiées complètes : cut/boundShort est propagé et cutTypos garde le résultat temporaire. Exception confirmée : LrTypoTape::refused ne fait qu’incrémenter stops, donc une coupure partagée des quatre successeurs peut être cachée comme complète. misread ignore aussi son compteur stops local. Ce défaut est centralisé sous COM-TYPO-INCOMPLET.

**Limite de preuve de ce périmètre.** Analyse statique uniquement ; aucun scénario exécuté ni coût mesuré.

**Contexte LR-057 — Borne inférieure par programmation dynamique.** La DP est une machine de phases explicite. Elle minimise sur les tokens de tous les modes, leurs suites after, suppressions et relectures, avec horizon et shifts de validation bornés. La borne enrichie est désactivée pour l’indentation/tokens laid. boundShort force le retour à la borne triviale et empêche les mémos incomplets. COM-TYPO-INCOMPLET est une exception : la coupure non signalée peut soustraire un candidat bon marché de la relaxation ; aucun choix final non optimal reproduit. Les références de frames ne sont pas conservées après une entrée qui peut agrandir oweStack. Le coût des transitions est imparfaitement représenté dans work.

**Limite de preuve de ce périmètre.** Analyse statique uniquement ; aucun scénario exécuté ni coût mesuré.

<a id="finding-core-ast-copy-tag"></a>

### CORE-AST-COPY-TAG — Une copie par base peut conserver le tag du type dérivé

**P2 · défaut · confiance forte.** Périmètres : AST-003.

**Mécanisme identifié.** AstNode expose copie et assignation defaulted, qui recopient type, parent et cstNodeId. Une copie C++ par valeur d’un objet dérivé vers une base concrète conserve donc le tag dérivé alors que le nouvel objet n’a plus ce type dynamique. ast_cast et le dispatch réflexif font ensuite confiance à ce tag pour effectuer un static_cast ou appliquer un offset. La copie du tag via le nœud n’est ni refusée ni remise à un état sans tag.

**Condition à vérifier.** Slicing d’un nœud estampillé ou assignation via une référence de base entre types dynamiques distincts. Ce chemin est extérieur à la construction normale du parser, qui est correctement estampillée.

**Impact.** Un cast peut annoncer un type absent de l’objet et autoriser un accès hors du sous-objet valide. Les liens parent/CST copiés peuvent aussi faire passer une copie pour un nœud du document d’origine.

**Direction de correction.** Définir une politique de copie des nœuds : l’interdire au niveau de la base, ou remettre l’identité et les liens à zéro puis fournir un clonage qui réestampille le type complet et reconstruit les liens. Éviter que la base publie une identité qu’elle ne peut vérifier.

**Sources à relire.**

- [src/core/cursiv/syntax-tree/AstNode.hpp:196](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/syntax-tree/AstNode.hpp#L196) — Copies publiques et champs copiés.
- [src/core/cursiv/syntax-tree/AstNode.hpp:311](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/syntax-tree/AstNode.hpp#L311) — Cast optimisé fondé sur le tag.
- [src/core/cursiv/syntax-tree/AstNode.hpp:764](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/syntax-tree/AstNode.hpp#L764) — ast_cast public accepte la preuve de tag.
- [src/core/cursiv/syntax-tree/DefaultAstReflection.hpp:42](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/syntax-tree/DefaultAstReflection.hpp#L42) — Dispatch des visiteurs par type estampillé.

**Contexte AST-003 — Tags de type et conversions vers les bases.** L’identité est l’adresse de ast_type_tag<T>, avec une fermeture dédupliquée des bases déclarées par Extends/VirtualExtends ; aucun hash de nom ne déclenche un cast. Pour l’héritage virtuel, les offsets sont obtenus sur un objet réel, écrits atomiquement, puis l’offset propre est publié release. Les lectures non initialisées retombent sur dynamic_cast. Les variantes ne recensent que les alternatives pointeurs vers AstNode, dans leur ordre de déclaration. Le protocole suppose que le type estampillé correspond au type complet et que les objets sont publiés après construction. La revue des opérations C++ publiques révèle néanmoins que la copie de base par défaut peut conserver un tag incompatible avec le nouvel objet ; les factories ne réparent pas une copie réalisée directement par le client.

**Limite de preuve de ce périmètre.** Le défaut vise la copie/slicing externe, pas l’instanciation normale par AstArena. Aucun crash ou appel de code arbitraire n’est allégué.

<a id="finding-core-completion-bounds"></a>

### CORE-COMPLETION-BOUNDS — La frontière de complétion LL/LR ne transporte ni annulation ni budget

**P2 · limite · confiance forte.** Périmètres : LL-046, LR-062, LR-065, LR-066.

**Mécanisme identifié.** Parser::nextFeaturesAt expose seulement CST, offsets et vecteur de sortie. Les deux moteurs sont appelés synchroniquement sans token ni quota de complétion. LL construit LlSimulation avec sa limite par défaut0 et rejoue le préfixe/les conditions. LR reconstruit ses CaretStacks, rejoue le préfixe et sature les piles/conditions/candidats sans poll propre. Les éventuels contrôles lexicaux internes ne bornent pas ces boucles ; RepairBudget concerne la réparation de parsing, pas cette API.

**Condition à vérifier.** Complétion sur un long préfixe, une grammaire ambiguë ou beaucoup de conditions ; la requête devient obsolète pendant le calcul.

**Impact.** Le client ne peut interrompre ces boucles une fois le calcul des features engagé. Aucun délai mural maximal ni ralentissement mesuré n’est déduit de cette absence de contrôle.

**Direction de correction.** Ajouter un contexte optionnel d’annulation/budget au contrat, le sonder dans les fermetures et boucles de replay, et distinguer résultat complet, interrompu ou limité.

**Sources à relire.**

- [src/core/cursiv/parser/Parser.hpp:49](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/parser/Parser.hpp#L49) — contrat sans cancellation
- [src/engines/cursiv/ll/runtime/LlLanguageParser.hpp:97](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/ll/runtime/LlLanguageParser.hpp#L97) — transmission synchrone
- [src/engines/cursiv/ll/runtime/ll_caret.hpp:950](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/ll/runtime/ll_caret.hpp#L950) — LlSimulation sans closureLimit
- [src/engines/cursiv/ll/runtime/ll_caret.hpp:951](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/ll/runtime/ll_caret.hpp#L951) — replays successifs
- [src/engines/cursiv/ll/runtime/ll_caret.hpp:1212](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/ll/runtime/ll_caret.hpp#L1212) — reprises de conditions
- [src/engines/cursiv/ll/table/LlSimulation.hpp:433](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/ll/table/LlSimulation.hpp#L433) — limite0 par défaut
- [src/engines/cursiv/lr/runtime/LrLanguageParser.hpp:64](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/LrLanguageParser.hpp#L64) — API sans token d’annulation.
- [src/engines/cursiv/lr/runtime/LrCaretTable.hpp:183](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/LrCaretTable.hpp#L183) — Contrat interne également sans budget.
- [src/engines/cursiv/lr/runtime/LrCaretTable.cpp:1908](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/LrCaretTable.cpp#L1908) — Rejeu du préfixe.
- [src/engines/cursiv/lr/runtime/LrCaretTable.cpp:1078](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/LrCaretTable.cpp#L1078) — Saturation sans poll.
- [src/engines/cursiv/lr/runtime/LrCaretTable.cpp:2394](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/LrCaretTable.cpp#L2394) — Boucles des candidats et spellings.

**Contexte LL-046 — Projection multibranche vers les features de complétion.** Les configurations sont projetées par terminal puis par identité de membre/type/narrow ; entered est combiné par accord des lectures. Un membre est pris au site immédiat ou au premier caller qui l’assigne ; une pile inconnue marque floor et impose un replay plus large. Les conditions conservent les survivants par terminal, dédupliqués par configuration exacte avant projection. Les terminaux EOF/indentation et chaînes vides ne deviennent pas des propositions. Les mots littéraux sont réinsérés et relexés pour vérifier frontières et commentaires. Pour les patterns sans graphie, les échantillons bornent la décision ; si aucun échantillon ne les représente, une condition indécidable laisse la proposition ouverte. L’API ne fournit en revanche aucun canal d’annulation ou d’incomplétude de ce travail.

**Limite de preuve de ce périmètre.** Pas de mesure de latence ni d’exécution. Les échantillons constituent une approximation assumée pour les patterns ; aucun faux négatif concret n’a été établi ici. La présente limite d’annulation ne suppose pas qu’une grammaire ordinaire soit lente.

Comparaison incluse : [LL-046](#comparison-ll-046).

**Contexte LR-062 — Frames et fermeture des piles au caret.** La clé de frame inclut état, offset, lookahead/mode, indentation, implicites, lecture et identités d’hypothèses/passes ; une collision de hash est vérifiée par égalité exacte. Les parents multiples sont dédupliqués, et l’ajout tardif d’un parent déclenche grown : relecture du nœud terminé et propagation aux dépendants de fermeture. below parcourt les niveaux de parents en triant/dédupliquant chaque front. mark/rollback restaure les tailles des frames/blocs/pool après un essai ; les worklists de fermeture sont réinitialisées avant le suivant. La saturation reste sans quota propre, couverte par CORE-COMPLETION-BOUNDS ; les casts 32 bits des IDs sont examinés avec les arènes runtime dans LR045.

**Limite de preuve de ce périmètre.** Analyse statique uniquement ; aucune requête de complétion ni grammaire de démonstration exécutée. Les coûts décrits sont structurels, pas mesurés.

**Contexte LR-065 — Rejeu du CST vers le caret.** Le rejeu est itératif et utilise les stamps pour les réparations, les tokens virtuels et les sauts de sous-arbre. Un saut exige un sous-arbre propre, une portée de conditions entièrement avant l’horizon, un premier token connu et une landing unique ; en échec il redescend aux enfants. Les événements récupérés sont conservés séparément et l’indentation est reportée lors des sauts. consume requiert des bornes lexicales exactes. Le défaut de projection aller-retour des bornes est centralisé sous CORE-TRANSLATION-ROUNDTRIP : l’échec de replay produit ici un retour sans features et aucun reparsing de secours.

**Limite de preuve de ce périmètre.** Analyse statique uniquement ; aucune requête de complétion ni grammaire de démonstration exécutée. Les coûts décrits sont structurels, pas mesurés.

**Contexte LR-066 — Essai de candidats et projection des features.** Les candidats passent par leurs spellings, l’élection lexicale, les guards/gaps, le maintien de la lecture précédente et les passes de réduction. Une feature est filtrée par sa landing et dédupliquée dans le groupe du terminal ; les différents sites/types ne sont pas écrasés. Les essais restaurent les piles par rollback. Les conditions peuvent être interrogées sur une valeur inconnue puis raffinées par échantillons, avec la limite d’exhaustivité décrite. Les coûts des dédups sont consolidés sous LR-CARET-CACHES-LINEAIRES et l’annulation sous CORE-COMPLETION-BOUNDS.

**Limite de preuve de ce périmètre.** Analyse statique uniquement ; aucune requête de complétion ni grammaire de démonstration exécutée. Les coûts décrits sont structurels, pas mesurés.

Comparaison incluse : [LR-066](#comparison-lr-066).

Anciennes clés regroupées : LL-COMPLETION-ANNULATION, LR-CARET-SANS-QUOTA.

<a id="finding-core-conversion-poll"></a>

### CORE-CONVERSION-POLL — Les parcours de conversion textuelle ne sondent pas l’annulation

**P2 · limite · confiance forte.** Périmètres : AST-010.

**Mécanisme identifié.** Les thunks de conversion parcourent des sous-arbres, découvrent des dépendances et concatènent des parties sans consulter de CancellationToken. L’observation du token par AstReducer entre lots de records n’interrompt pas une conversion déjà engagée. Les parcours sont explicites et évitent une récursion native générale, mais cela ne borne pas la latence d’annulation.

**Condition à vérifier.** Règle data couvrant un grand sous-arbre, nombreuses parties ou chaîne de conversions de texte. Les callbacks personnalisés ont en outre leur propre coût.

**Impact.** L’annulation du parse peut attendre un travail proportionnel au sous-arbre et aux octets produits par le thunk en cours. Aucune durée maximale ou lenteur mesurée n’est revendiquée.

**Direction de correction.** Transmettre un poll d’annulation au contexte de conversion et le consulter pendant la collecte de parties, les dépendances et les grosses copies ; préciser le contrat des converters utilisateurs.

**Sources à relire.**

- [src/engines/cursiv/shared/runtime/AstConvThunks.hpp:119](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/shared/runtime/AstConvThunks.hpp#L119) — Parcours des parties sans token.
- [src/engines/cursiv/shared/runtime/AstConvThunks.hpp:154](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/shared/runtime/AstConvThunks.hpp#L154) — Évaluation itérative des dépendances sans polling.
- [src/engines/cursiv/shared/runtime/AstReducer.hpp:542](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/shared/runtime/AstReducer.hpp#L542) — Polling externe par lots de records.

**Contexte AST-010 — Conversions scalaires et erreurs de conversion.** La conversion numérique exige la consommation complète, traite le plus initial une seule fois, refuse les doubles signes et conserve les erreurs de plage. Le fast path double limite significande et exposant et retombe sur from_chars si l’arrondi hôte ne convient pas ; float évite le double arrondi. Les booléens acceptent la casse ASCII et les enum utilisent les noms sondés. ConversionResult produit un diagnostic au record et laisse le membre précédent si la conversion échoue. Une string possédée convertie en string_view est copiée dans le pool du CST. La propagation des exceptions de converters est distincte du canal ConversionResult. Les grandes marches textuelles n’observent pas le token interne.

**Limite de preuve de ce périmètre.** Aucune exactitude exhaustive de toutes les combinaisons de décimaux/plateformes n’est prouvée. Le périmètre ne valide pas les callbacks métier arbitraires.

Comparaison incluse : [AST-010](#comparison-ast-010).

<a id="finding-core-gen-partial-publish"></a>

### CORE-GEN-PARTIAL-PUBLISH — Un échec de génération peut laisser un ensemble de fichiers partiellement renouvelé

**P2 · risque · confiance forte.** Périmètres : GRA-012.

**Mécanisme identifié.** write_source ouvre directement la destination en écriture et CommonArtifacts publie les fichiers un à un. L’état du flux est correctement vérifié après close, mais aucun remplacement atomique par fichier ni transaction de l’ensemble n’est prévu. Une erreur tardive laisse les premiers fichiers neufs et les suivants anciens, et peut tronquer celui qui échoue.

**Condition à vérifier.** Erreur d’écriture/fermeture, manque d’espace ou interruption pendant la publication.

**Impact.** Le répertoire généré est incohérent jusqu’à une génération réussie ; une consommation hors des dépendances CMake peut lire un mélange de versions.

**Direction de correction.** Produire dans un répertoire temporaire puis publier après succès, ou à minima écrire chaque fichier temporaire puis renommer et marquer l’ensemble incomplet jusqu’à la fin.

**Sources à relire.**

- [src/engines/cursiv/shared/gen/write_source.hpp:22](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/shared/gen/write_source.hpp#L22) — Ouverture directe puis contrôle de fermeture.
- [src/engines/cursiv/shared/gen/CommonArtifacts.cpp:166](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/shared/gen/CommonArtifacts.cpp#L166) — Publication séquentielle avec arrêt au premier échec.

**Contexte GRA-012 — Publication des fichiers générés.** write_source compare taille puis contenu binaire et ne touche pas un fichier identique. L’écriture vérifie l’ouverture et la fermeture, donc les erreurs différées de disque ne sont pas silencieuses. CommonArtifacts s’arrête au premier échec et retourne le groupe concerné, mais certains groupes regroupent plusieurs fichiers et ne donnent pas le nom précis. La publication partielle reste possible et son impact dépend du système de build consommateur.

**Limite de preuve de ce périmètre.** Fichiers dérivés régénérables : il ne s’agit pas d’une perte de sources auteur. Aucun défaut de remontée de close n’est retenu.

<a id="finding-core-grammar-override-diamond"></a>

### CORE-GRAMMAR-OVERRIDE-DIAMOND — Un override local ne résout pas le conflit entre deux overrides parents

**P2 · défaut · confiance forte.** Périmètres : GRA-002.

**Mécanisme identifié.** choose_parent refuse deux déclarations parentes distinctes en conseillant de redéfinir la règle dans la grammaire enfant. Pourtant eff_body<G,R,true> instancie toujours winners_t<R,G> pour calculer le corps parent avant plug_t, même si le corps de l’override enfant ne contient aucun super. Le correctif indiqué au consommateur reste donc refusé.

**Condition à vérifier.** Héritage de deux grammaires qui redéfinissent séparément la même règle, avec override explicite dans l’enfant. Le diamant avec une seule déclaration commune est correctement dédupliqué.

**Impact.** Composition de grammaires impossible par l’override local annoncé ; diagnostic proposant une action inefficace.

**Direction de correction.** Ne rechercher un corps parent non ambigu que si super est utilisé, ou prévoir une sélection explicite du parent et adapter le diagnostic.

**Sources à relire.**

- [src/core/cursiv/grammar/GrammarUtils.hpp:66](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/grammar/GrammarUtils.hpp#L66) — Choix du parent et conseil de redéfinition.
- [src/core/cursiv/grammar/GrammarUtils.hpp:150](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/grammar/GrammarUtils.hpp#L150) — Instanciation inconditionnelle de winners_t dans le cas override local.
- [src/core/cursiv/grammar/GrammarUtils.hpp:163](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/grammar/GrammarUtils.hpp#L163) — Le slot court-circuite les parents, le corps ne le fait pas.

**Contexte GRA-002 — Identité des règles et composition de grammaires.** L’identité déclarée est préservée récursivement à travers les stages ; les corps effectifs, types et converters suivent la déclaration d’override pertinente. one_per_decl déduplique les deux chemins d’un diamant qui rejoignent réellement la même déclaration. InGrammar ajoute la grammaire d’origine à l’identité, requalifie les références et garde ses frontières de mots/trivia ; l’indentation imbriquée est refusée explicitement. Le cas de deux overrides parents distincts révèle le défaut confirmé par une seconde lecture indépendante.

**Limite de preuve de ce périmètre.** Aucun compilateur lancé ; le défaut découle de la dépendance obligatoire des arguments de template.

Comparaison incluse : [GRA-002](#comparison-gra-002).

<a id="finding-core-grammar-runtime-model"></a>

### CORE-GRAMMAR-RUNTIME-MODEL — L’API effacée n’expose pas le modèle structurel complet de la grammaire

**P2 · écart d’API · confiance forte.** Périmètres : GRA-007.

**Mécanisme identifié.** Grammar expose des questions sur records/terminaux et GrammarSource expose texte, noms et fermetures. Aucun de ces contrats ne permet de parcourir règles, alternatives, cardinalités et actions comme l’AST Grammar de Langium. GrammarImage requiert les types/descripteurs du générateur et n’est pas la propriété structurelle du service Parser effacé.

**Condition à vérifier.** Provider générique, outil de migration ou analyse de grammaire qui ne connaît pas les types C++ de la langue.

**Impact.** Il faut exposer une métadonnée supplémentaire ou coupler l’outil au générateur ; relire le dump n’est pas une API structurelle stable.

**Direction de correction.** Définir un modèle runtime optionnel ou documenter précisément le sous-ensemble d’introspection et fournir des adaptateurs pour les usages génériques.

**Sources à relire.**

- [src/core/cursiv/grammar/Grammar.hpp:17](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/grammar/Grammar.hpp#L17) — Questions disponibles dans le contrat effacé.
- [src/core/cursiv/grammar/DefaultGrammarSource.cpp:7](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/grammar/DefaultGrammarSource.cpp#L7) — Texte et listes de règles.
- [src/core/cursiv/grammar/ImageOf.hpp:500](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/grammar/ImageOf.hpp#L500) — Image créée à partir d’une vue C++ connue.

**Contexte GRA-007 — Entrées et introspection de la grammaire.** Les identités de terminal sont interprétées via terminalName/Spellings/Doc ; les records via les méthodes spécifiques writes/fills/rule/feature, sans confondre leurs espaces d’IDs. NextFeature transporte propriété, type et narrow pour conserver le contexte d’une référence même avant la construction du nœud. Les métadonnées générées sont constantes et leur durée de vie couvre les résultats. Le commentaire Parser évoquant getEntryRule est périmé : cette méthode ne figure pas dans Grammar.

**Limite de preuve de ce périmètre.** La stabilité des numéros entre deux générations n’est pas promise ; ils ne constituent pas des identifiants persistants.

Comparaison incluse : [GRA-007](#comparison-gra-007).

<a id="finding-core-text-size-domain"></a>

### CORE-TEXT-SIZE-DOMAIN — TextDocument accepte un texte hors du domaine de ses offsets 32 bits

**P2 · écart d’API · confiance forte.** Périmètres : TXT-004.

**Mécanisme identifié.** setText accepte une std::string de taille arbitraire, tandis que size la convertit en TextOffset uint32_t sans rejet. La construction des index et les conversions publient aussi des offsets 32 bits. La limite de taille imposée plus tard au CST ne protège pas l’API TextDocument utilisée directement.

**Condition à vérifier.** Texte supérieur à UINT32_MAX octets sur un hôte 64 bits disposant de la mémoire nécessaire, à la création ou après édition.

**Impact.** size et positions peuvent décrire un préfixe tronqué ou replié du texte réellement conservé.

**Direction de correction.** Rejeter la taille excédentaire avant de remplacer le texte, et appliquer cette même borne aux chemins de création et d’édition.

**Sources à relire.**

- [src/core/cursiv/workspace/TextDocument.hpp:124](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/TextDocument.hpp#L124) — size réduit size_t à TextOffset.
- [src/core/cursiv/workspace/TextDocument.cpp:188](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/TextDocument.cpp#L188) — setText ne valide pas la taille.
- [src/core/cursiv/text/TextOffset.hpp:12](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/text/TextOffset.hpp#L12) — Domaine 32 bits explicite.

**Contexte TXT-004 — Index des lignes d'un TextDocument.** L’index des lignes utilise un mutex et un double contrôle acquire/release. Les vecteurs sont remis à zéro avant reconstruction ; le ready n’est publié qu’après leur achèvement, et une exception laisse la reconstruction possible. CRLF compte comme une seule fin de ligne ; CR et LF seuls sont reconnus. Les tables ASCII/bien-formé évitent des décodages superflus. La synchronisation protège l’initialisation entre lecteurs, mais les lectures ultérieures supposent la stabilité du document sous le verrou du workspace.

**Limite de preuve de ce périmètre.** L’indépendance de lectures concurrentes et d’une mutation exige le protocole workspace ; aucun débit ni contention mesurés.

<a id="finding-core-translation-roundtrip"></a>

### CORE-TRANSLATION-ROUNDTRIP — La complétion perd des positions après une traduction non inversible

**P2 · défaut · confiance forte.** Périmètres : CST-007, LR-065.

**Mécanisme identifié.** SourceTranslation élargit les bornes intérieures d’une pièce remplacée vers ses bornes source. Parser::engine_tree applique ensuite to_text aux spans déjà projetés, comme si cette projection était inversible. Plusieurs tokens d’une même pièce peuvent donc recevoir la même grande plage reconstruite. Le replay LR exige au contraire une égalité exacte entre bornes du token relexé et de la feuille ; son échec retourne sans proposition, sans reconstituer les anciennes bornes moteur.

**Condition à vérifier.** Traduction remplaçant une pièce source par plusieurs tokens et complétion dont le préfixe rejoue une de ces feuilles directement ; par exemple des mots-clés directs de la règle d’entrée, sans sous-règle antérieure sautant le fragment.

**Impact.** Des continuations valides peuvent disparaître après la traduction, alors que le parsing du texte traduit a réussi. La perte de positions est établie par la lecture du mapping et du consommateur LR.

**Direction de correction.** Conserver les plages originales du CST moteur à côté de la projection documentaire, ou conserver le CST moteur pour la complétion ; ne pas reconstruire ces plages depuis une application plusieurs-vers-un.

**Sources à relire.**

- [src/core/cursiv/syntax-tree/SourceTranslation.cpp:19](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/syntax-tree/SourceTranslation.cpp#L19) — Projection d’une borne intérieure vers une extrémité.
- [src/core/cursiv/parser/Parser.cpp:25](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/parser/Parser.cpp#L25) — Reconstruction depuis les plages documentaires.
- [src/engines/cursiv/lr/runtime/LrCaretTable.cpp:214](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/LrCaretTable.cpp#L214) — Égalité exacte au shift.
- [src/engines/cursiv/lr/runtime/LrCaretTable.cpp:1947](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/LrCaretTable.cpp#L1947) — Rejeu de feuille propre par consume.
- [src/engines/cursiv/lr/runtime/LrCaretTable.cpp:2308](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/LrCaretTable.cpp#L2308) — Retour sans résultats si le replay échoue.

**Contexte CST-007 — Projection des positions après traduction source.** La projection élargit volontairement toute borne intérieure d’une pièce remplacée vers les bornes source ; translated refuse un caret strictement intérieur. Le fast path déplace les spans dans un intervalle entre pièces et revient au parcours général lorsque les fins des diagnostics reculent. Le raccord de complétion tente ensuite de reconstruire les bornes moteur depuis ces seules plages élargies : l’information perdue n’est pas restituable. La relecture du replay LR confirme l’effet fonctionnel lorsque les feuilles doivent être consommées une à une. Le saut d’une sous-règle peut éviter certains cas, mais ne couvre pas les feuilles directes de l’entrée.

**Limite de preuve de ce périmètre.** Raisonnement statique croisé avec l’audit LR, sans reproducer ni exécution. Les conditions précisent le chemin atteint ; aucune généralisation à toutes les traductions ni à toutes les complétions LL n’est affirmée.

**Contexte LR-065 — Rejeu du CST vers le caret.** Le rejeu est itératif et utilise les stamps pour les réparations, les tokens virtuels et les sauts de sous-arbre. Un saut exige un sous-arbre propre, une portée de conditions entièrement avant l’horizon, un premier token connu et une landing unique ; en échec il redescend aux enfants. Les événements récupérés sont conservés séparément et l’indentation est reportée lors des sauts. consume requiert des bornes lexicales exactes. Le défaut de projection aller-retour des bornes est centralisé sous CORE-TRANSLATION-ROUNDTRIP : l’échec de replay produit ici un retour sans features et aucun reparsing de secours.

**Limite de preuve de ce périmètre.** Analyse statique uniquement ; aucune requête de complétion ni grammaire de démonstration exécutée. Les coûts décrits sont structurels, pas mesurés.

<a id="finding-core-unicode-output-path"></a>

### CORE-UNICODE-OUTPUT-PATH — Le générateur Unicode vise un répertoire absent de l’arborescence actuelle

**P2 · défaut · confiance forte.** Périmètres : TXT-003.

**Mécanisme identifié.** unicode_dir contient src/core/cursiv/core/grammar/unicode, alors que les headers consommés se trouvent dans src/core/cursiv/grammar/unicode. Le premier open en écriture ne crée pas le répertoire intermédiaire.

**Condition à vérifier.** Régénération avec tools/gen_unicode_ranges.py dans ce snapshot.

**Impact.** La régénération ne remplace pas les tables utilisées ; en l’absence du répertoire supplémentaire elle échoue à l’ouverture du fichier.

**Direction de correction.** Corriger le chemin et vérifier explicitement que la cible correspond aux headers inclus avant toute écriture.

**Sources à relire.**

- [tools/gen_unicode_ranges.py:55](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/tools/gen_unicode_ranges.py#L55) — Segment core supplémentaire.
- [tools/gen_unicode_ranges.py:75](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/tools/gen_unicode_ranges.py#L75) — Première ouverture sans création du dossier.
- [src/core/cursiv/grammar/unicode/UnicodeRange.hpp:16](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/grammar/unicode/UnicodeRange.hpp#L16) — Lookup des tables dans la véritable arborescence.

**Contexte TXT-003 — Génération et bornes des tables Unicode.** Les ranges sont coalescées à partir de codepoints triés ; les catégories majeures excluent Cs, XID_Start écarte explicitement underscore et White_Space retire U+001C–001F du résultat Python. within recherche la première borne haute >= cp, puis vérifie la borne basse ; il suppose les intervalles triés et disjoints. Les runs de minuscules encodent delta/stride et isolent l’expansion U+0130. Deux défauts de maintenance de la chaîne de génération sont établis ci-dessous.

**Limite de preuve de ce périmètre.** Aucune régénération ni comparaison exhaustive avec les fichiers UCD n’a été exécutée ; aucune erreur de cellule particulière n’est affirmée.

<a id="finding-core-uri-localhost"></a>

### CORE-URI-LOCALHOST — localhost ne disparaît pas de la clé URI malgré le contrat public

**P2 · défaut · confiance forte.** Périmètres : TXT-009.

**Mécanisme identifié.** normalize_uri conserve l’autorité localhost, y compris via son retour rapide is_normal_uri. file_uri_to_path traite pourtant localhost et l’autorité vide comme le même chemin local. Le header promet explicitement une forme unique file:///path.

**Condition à vérifier.** Même fichier référencé avec file:///path et file://localhost/path.

**Impact.** Les maps de documents, égalités et relations de parenté peuvent considérer deux identités pour le même fichier local.

**Direction de correction.** Normaliser localhost vers l’autorité vide pour le schéma file, dans le chemin rapide comme dans le chemin de réécriture.

**Sources à relire.**

- [src/core/cursiv/utils/UriUtils.hpp:13](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/utils/UriUtils.hpp#L13) — Promesse publique.
- [src/core/cursiv/utils/UriUtils.cpp:304](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/utils/UriUtils.cpp#L304) — Autorité conservée.
- [src/core/cursiv/utils/UriUtils.cpp:342](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/utils/UriUtils.cpp#L342) — Les deux autorités ouvrent le même chemin local.

**Contexte TXT-009 — Canonicalisation des URI.** La décomposition des composants précède le percent-decode ; les # et ? échappés restent dans le nom de fichier. Le NUL décodé du chemin est rejeté avant accès système. Le schéma et l’hôte sont abaissés ; userinfo conserve sa casse ; la lettre de lecteur est normalisée indépendamment de l’hôte. Les chemins nus ne reçoivent pas de schéma inventé. En revanche, l’équivalence locale promise pour localhost est absente.

**Limite de preuve de ce périmètre.** Pas de qualification exhaustive RFC de tous les schémas virtuels, ni de validation native Windows exécutée.

<a id="finding-core-uri-trie-depth"></a>

### CORE-URI-TRIE-DEPTH — Le trie d’URI conserve une profondeur de pile proportionnelle au chemin

**P2 · limite · confiance forte.** Périmètres : TXT-010.

**Mécanisme identifié.** Les recherches et insertions descendent itérativement, mais appendValues s’appelle récursivement pour chaque segment. La destruction des unique_ptr imbriqués suit également cette profondeur. Aucun plafond de segments n’est imposé.

**Condition à vérifier.** URIs virtuelles ou entrées externes comportant une très grande profondeur de segments.

**Impact.** all/findAll et suppression/destruction peuvent épuiser la pile native indépendamment du nombre de valeurs à rendre.

**Direction de correction.** Itérer explicitement pour la collecte et la destruction, ou établir une borne d’admission explicite et commune.

**Sources à relire.**

- [src/core/cursiv/utils/UriTrie.hpp:101](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/utils/UriTrie.hpp#L101) — Ownership imbriqué des enfants.
- [src/core/cursiv/utils/UriTrie.hpp:162](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/utils/UriTrie.hpp#L162) — Collecte récursive.

**Contexte TXT-010 — Parenté et chemins relatifs d'URI.** contains_uri exige même schéma et autorité, puis une frontière slash : /a/b ne contient pas /a/bc. relative_uri compare les segments décodés, remonte avec ../ et refuse une relativisation entre autorités ou lecteurs différents. Le trie normalise ses clés, répare les parents des enfants après déplacement et élimine les épines vides après eraseValue. La collecte et la destruction restent récursives.

**Limite de preuve de ce périmètre.** Le contrat reste lexical : pas de résolution réelle de symlinks ni de normalisation générale des segments . et .. dans normalize_uri.

<a id="finding-exa-arith-profondeur"></a>

### EXA-ARITH-PROFONDEUR — Les parcours arithmétiques reprennent une pile native proportionnelle à la profondeur

**P2 · limite · confiance forte.** Périmètres : EXA-002, EXA-004, EXA-005, EXA-006.

**Mécanisme identifié.** evaluate, select_calls, fold et walk_divisions descendent récursivement dans l’AST. Le budget de64 ne s’applique qu’aux évaluations de diviseurs ; il ne borne ni le parcours externe des expressions ni l’évaluation CLI. evaluate_call copie aussi le vecteur des définitions actives et le recherche linéairement à chaque appel.

**Condition à vérifier.** Expressions associatives très longues ou chaînes profondes de définitions acycliques.

**Impact.** Pression sur la pile native ; chaînes d’appels avec coût cumulé quadratique des copies/recherches actives. Aucun seuil pratique mesuré.

**Direction de correction.** Employer des piles explicites, propager annulation et budget sur les parcours, et gérer l’ensemble actif par entrée/sortie de frame.

**Sources à relire.**

- [examples/arithmetics/src/core/arithmetics/Interpreter.cpp:45](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/arithmetics/src/core/arithmetics/Interpreter.cpp#L45) — Recherche des appels actifs.
- [examples/arithmetics/src/core/arithmetics/Interpreter.cpp:52](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/arithmetics/src/core/arithmetics/Interpreter.cpp#L52) — Copie de la pile active.
- [examples/arithmetics/src/core/arithmetics/Interpreter.cpp:85](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/arithmetics/src/core/arithmetics/Interpreter.cpp#L85) — Récursion des expressions.
- [examples/arithmetics/src/core/arithmetics/validation/ArithmeticsValidator.cpp:65](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/arithmetics/src/core/arithmetics/validation/ArithmeticsValidator.cpp#L65) — Récursion des appels.
- [examples/arithmetics/src/core/arithmetics/validation/ArithmeticsValidator.cpp:138](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/arithmetics/src/core/arithmetics/validation/ArithmeticsValidator.cpp#L138) — Récursion de fold.
- [examples/arithmetics/src/core/arithmetics/validation/ArithmeticsValidator.cpp:198](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/arithmetics/src/core/arithmetics/validation/ArithmeticsValidator.cpp#L198) — Parcours externe non borné par64.

**Contexte EXA-002 — Évaluation numérique, liaison des arguments et fin d’évaluation.** Les arguments sont évalués dans le contexte appelant et le corps dans un contexte neuf indexé par identité de paramètre ; aucune fuite par homonymie n’est visible. Une référence absente, une arité incorrecte, un cycle actif ou un budget épuisé retourne nullopt. Le compteur partagé facture une unité par expression et suit les appels. Les opérations utilisent double, avec garde spécifique pour division/modulo par zéro ; pow peut retourner un non-fini. La projection CLI perd la raison et même la présence des échecs. L’évaluation CLI est sans budget et conserve des parcours récursifs.

**Limite de preuve de ce périmètre.** Lecture statique uniquement ; aucune exécution du projet ni mesure de coût.

**Contexte EXA-004 — Construction du graphe d’appels et signalement de la récursion.** Le graphe développe chaque définition une seule fois via expanded, lie chaque appel à son parent et remonte ces liens pour produire le chemin diagnostiqué. Les cycles directs sont traités explicitement ; les listes d’ouverture évitent une récursion C++ pour le graphe lui-même. En revanche, le collecteur des appels s’arrête au premier FunctionCall et ignore les arguments, en contradiction avec l’évaluation eager. La remontée des parents peut coûter une profondeur par arête ; aucune promesse de linéarité générale n’est établie.

**Limite de preuve de ce périmètre.** Lecture statique uniquement ; aucune exécution du projet ni mesure de coût.

**Contexte EXA-005 — Recherche des divisions et modulos par zéro sous budget.** La visite couvre définitions et évaluations, groupements, deux opérandes et arguments des appels. Chaque nœud visité vérifie l’annulation ; le diviseur est évalué avec64 pas partagés à travers les appels. Seul un résultat présent exactement égal à0 produit un diagnostic sur l’opérande droit ; inconnu, cycle et épuisement sont volontairement inconclusifs. Le coût annoncé O(divisions) est trop étroit : le parcours externe visite tous les nœuds, donc O(nœuds+64×divisions), et sa profondeur native n’est pas bornée par le budget.

**Limite de preuve de ce périmètre.** Lecture statique uniquement ; aucune exécution du projet ni mesure de coût.

**Contexte EXA-006 — Normalisation constante jusqu’à l’édition de remplacement.** Le fold est ascendant et retire les résultats enfants lorsqu’un parent peut les remplacer ; les valeurs non finies et divisions fautives sont exclues. Les informations de normalisation portent une donnée numérique structurée ; l’action utilise les diagnostics courants du document, filtre code, intersection de plage et type de donnée. Les appels ne sont pas traversés par fold, ce qui limite les suggestions aux sous-expressions binaires/groupées des corps de définitions. La réémission du résultat ignore cependant la syntaxe de NUMBER et casse le contrat de transformation.

**Limite de preuve de ce périmètre.** Lecture statique uniquement ; aucune exécution du projet ni mesure de coût.

<a id="finding-exa-cli-exceptions"></a>

### EXA-CLI-EXCEPTIONS — Les sous-commandes des exemples échappent à la gestion d’erreurs commune

**P2 · défaut · confiance forte.** Périmètres : EXA-002, EXA-013, EXA-030, EXA-031.

**Mécanisme identifié.** Les branches generate et eval initialisent les services et appellent directement leurs opérations depuis main, avant run_cli. UsageError et erreurs filesystem/générateur ne traversent donc pas les catches de run_cli_on. Elles omettent aussi la vérification des flux de sortie que fournit report.

**Condition à vérifier.** Chemin source inexistant pour eval, racine invalide ou échec create_directories/ouverture pour generate.

**Impact.** Exception standard non rattrapée jusqu’à main, terminaison du processus au lieu des codes2/3 annoncés par la CLI ; un échec stdout peut aussi passer comme succès.

**Direction de correction.** Faire passer les commandes spécialisées par un exécuteur commun assurant initialisation, catches et vérification des flux.

**Sources à relire.**

- [examples/arithmetics/src/cli.cpp:22](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/arithmetics/src/cli.cpp#L22) — eval avant run_cli.
- [examples/domainmodel/src/cli.cpp:79](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/domainmodel/src/cli.cpp#L79) — generate avant run_cli.
- [examples/statemachine/src/cli.cpp:82](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/statemachine/src/cli.cpp#L82) — Même bifurcation.
- [examples/requirements/src/cli.cpp:79](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/requirements/src/cli.cpp#L79) — Même bifurcation.
- [src/cli/cursiv/CliUtils.cpp:509](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/cli/cursiv/CliUtils.cpp#L509) — Gestion commune absente de ces chemins.
- [src/cli/cursiv/CliUtils.cpp:549](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/cli/cursiv/CliUtils.cpp#L549) — Traduction en codes2/3.

**Contexte EXA-002 — Évaluation numérique, liaison des arguments et fin d’évaluation.** Les arguments sont évalués dans le contexte appelant et le corps dans un contexte neuf indexé par identité de paramètre ; aucune fuite par homonymie n’est visible. Une référence absente, une arité incorrecte, un cycle actif ou un budget épuisé retourne nullopt. Le compteur partagé facture une unité par expression et suit les appels. Les opérations utilisent double, avec garde spécifique pour division/modulo par zéro ; pow peut retourner un non-fini. La projection CLI perd la raison et même la présence des échecs. L’évaluation CLI est sans budget et conserve des parcours récursifs.

**Limite de preuve de ce périmètre.** Lecture statique uniquement ; aucune exécution du projet ni mesure de coût.

**Contexte EXA-013 — Correspondance packages Java et arborescence des fichiers.** Un package pointé devient une arborescence et les packages imbriqués sont concaténés ; sous séparateurs /, la déclaration package et les répertoires concordent à partir du nom de fichier racine. Ce dernier n’est pas un identifiant Java garanti, et la conversion du chemin natif n’est pas portable. Les fichiers sont ouverts en troncature sans détection des collisions de types/noms de stem ; a-b et ab convergent. Le contrôle final d’écriture est absent dans les trois générateurs. Les commandes spécialisées ne bénéficient pas des catches communs.

**Limite de preuve de ce périmètre.** Lecture statique uniquement ; aucune exécution du projet ni mesure de coût.

**Contexte EXA-030 — Choix et publication du fichier C++ généré.** La destination par défaut est generated ; le nom de sortie est le stem privé de points/tirets avec fallback generated, puis .cpp. Le chemin peut converger entre plusieurs sources et l’ouverture tronque un fichier déjà présent. La génération est calculée après l’ouverture, donc une exception de contenu peut laisser une ancienne sortie tronquée. Les create_directories/open peuvent échouer ; seule l’ouverture est contrôlée et le chemin est retourné sans flush/close vérifié. Les mêmes choix de nom, écriture et gestion des exceptions sont dupliqués dans les trois exemples.

**Limite de preuve de ce périmètre.** Lecture statique uniquement ; aucune exécution du projet ni mesure de coût.

**Contexte EXA-031 — Choix et publication du rapport HTML.** Le rapport est placé sous la destination explicite ou generated et porte le stem sans points/tirets, suivi de .html ; un stem vidé devient generated. Cette normalisation fait converger a-b.req et ab.req et l’ouverture remplace une sortie existante. create_directories peut lever ; l’ouverture seule est contrôlée, pas la publication effective du HTML. Les erreurs de génération échappent au handler CLI commun et les erreurs de validation du périmètre n’empêchent pas le succès. Les helpers de destination/normalisation et la séquence d’écriture sont dupliqués avec domainmodel/statemachine.

**Limite de preuve de ce périmètre.** Lecture statique uniquement ; aucune exécution du projet ni mesure de coût.

<a id="finding-exa-cycles-arguments"></a>

### EXA-CYCLES-ARGUMENTS — La détection de récursion ignore les appels présents dans les arguments

**P2 · défaut · confiance forte.** Périmètres : EXA-004.

**Mécanisme identifié.** select_calls ajoute l’appel externe puis retourne sans visiter call.args. Pourtant l’interpréteur évalue ces arguments avant le corps. Le graphe statique omet donc des dépendances exécutées.

**Condition à vérifier.** def id(x): x; def a: id(a);. Le corps de a fournit l’arête a→id mais pas a→a, alors que l’évaluation de l’argument rappelle a.

**Impact.** Une récursion interdite est acceptée par la validation ; l’interpréteur l’arrête par nullopt, ensuite silencieux en CLI.

**Direction de correction.** Visiter récursivement chaque argument en conservant la définition hôte ; auditer séparément le calcul des composantes cycliques.

**Sources à relire.**

- [examples/arithmetics/src/core/arithmetics/validation/ArithmeticsValidator.cpp:51](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/arithmetics/src/core/arithmetics/validation/ArithmeticsValidator.cpp#L51) — Exclusion explicite des arguments.
- [examples/arithmetics/src/core/arithmetics/validation/ArithmeticsValidator.cpp:291](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/arithmetics/src/core/arithmetics/validation/ArithmeticsValidator.cpp#L291) — Le graphe repose exclusivement sur select_calls.
- [examples/arithmetics/src/core/arithmetics/Interpreter.cpp:58](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/arithmetics/src/core/arithmetics/Interpreter.cpp#L58) — Les arguments sont effectivement évalués.

**Contexte EXA-004 — Construction du graphe d’appels et signalement de la récursion.** Le graphe développe chaque définition une seule fois via expanded, lie chaque appel à son parent et remonte ces liens pour produire le chemin diagnostiqué. Les cycles directs sont traités explicitement ; les listes d’ouverture évitent une récursion C++ pour le graphe lui-même. En revanche, le collecteur des appels s’arrête au premier FunctionCall et ignore les arguments, en contradiction avec l’évaluation eager. La remontée des parents peut coûter une profondeur par arête ; aucune promesse de linéarité générale n’est établie.

**Limite de preuve de ce périmètre.** Lecture statique uniquement ; aucune exécution du projet ni mesure de coût.

<a id="finding-exa-domain-profondeur"></a>

### EXA-DOMAIN-PROFONDEUR — La qualification multiplie les noms stockés et les parcours récursifs

**P2 · limite · confiance forte.** Périmètres : EXA-009, EXA-011, EXA-013.

**Mécanisme identifié.** processContainer remonte les descriptions de tous les descendants à chaque ancêtre, en fabriquant et conservant une nouvelle chaîne qualifiée. qualifierOf, qualified_after_rename, collect_subtree et generate_abstract_elements descendent ou remontent récursivement les packages.

**Condition à vérifier.** Un type au fond de D packages imbriqués : D variantes de noms, de longueur croissante, et profondeur native D ; plusieurs types multiplient ces coûts.

**Impact.** Volume cumulé des chaînes quadratique en profondeur pour des noms de taille constante ; pression sur la pile pour scopes, rename et génération. Aucun seuil pratique établi.

**Direction de correction.** Représenter les qualifications par segments partagés et utiliser des parcours itératifs ; expliciter les budgets ou limites de profondeur.

**Sources à relire.**

- [examples/domainmodel/src/core/domainmodel/references/DomainModelScopeComputation.cpp:132](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/domainmodel/src/core/domainmodel/references/DomainModelScopeComputation.cpp#L132) — Chaque niveau recrée et conserve les noms descendants.
- [examples/domainmodel/src/core/domainmodel/references/QualifiedNameProvider.cpp:19](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/domainmodel/src/core/domainmodel/references/QualifiedNameProvider.cpp#L19) — Récursion de qualification.
- [examples/domainmodel/src/lsp/domainmodel/DomainModelRenameProvider.cpp:43](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/domainmodel/src/lsp/domainmodel/DomainModelRenameProvider.cpp#L43) — Qualification récursive après renommage.
- [examples/domainmodel/src/lsp/domainmodel/DomainModelRenameProvider.cpp:55](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/domainmodel/src/lsp/domainmodel/DomainModelRenameProvider.cpp#L55) — Collecte récursive.
- [examples/domainmodel/src/cli/domainmodel/Generator.cpp:142](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/domainmodel/src/cli/domainmodel/Generator.cpp#L142) — Génération récursive.

**Contexte EXA-009 — Qualification et visibilité des types dans les packages.** Les exports couvrent toute la famille Type, où qu’elle soit contenue, et qualifient entièrement les types sous package. Les scopes locaux contiennent des noms simples au niveau de déclaration et des qualifications partielles remontées à chaque ancêtre. Les chaînes calculées sont conservées dans le pool du document avant publication ; elles ne pointent pas sur les std::string temporaires. La comparaison des références est explicitement insensible à la casse. Les doublons de types ne sont pas validés par cet exemple ; les problèmes de projection Java sont suivis en EXA-012. Les packages profonds ont un coût de duplication et de pile explicite.

**Limite de preuve de ce périmètre.** Lecture statique uniquement ; aucune exécution du projet ni mesure de coût.

**Contexte EXA-011 — Renommage des packages et propagation des noms qualifiés.** Le provider demande d’abord les edits de déclaration au provider de base puis collecte le sous-arbre si un package est renommé. La nouvelle qualification est calculée sans mutation AST. Pour chaque référence, un nom écrit avec point devient entièrement qualifié et un nom nu conserve la forme propre de sa cible, modifiée seulement si cette cible est celle renommée. Les documents sources sont récupérés par ID ; la plage est vérifiée avant lecture. Les edits de même position initiale remplacent le texte du provider de base au lieu d’ajouter un chevauchement. La représentation pleinement qualifiée peut allonger un nom auparavant relatif, mais elle reste exportée par les scopes lus. Aucun défaut propre de propagation démontré ; les profondeurs sont celles de EXA-009.

**Limite de preuve de ce périmètre.** Validité générale de nouveaux noms/version LSP auditée dans le provider commun ; aucun conflit de résolution après renommage reproduit.

**Contexte EXA-013 — Correspondance packages Java et arborescence des fichiers.** Un package pointé devient une arborescence et les packages imbriqués sont concaténés ; sous séparateurs /, la déclaration package et les répertoires concordent à partir du nom de fichier racine. Ce dernier n’est pas un identifiant Java garanti, et la conversion du chemin natif n’est pas portable. Les fichiers sont ouverts en troncature sans détection des collisions de types/noms de stem ; a-b et ab convergent. Le contrôle final d’écriture est absent dans les trois générateurs. Les commandes spécialisées ne bénéficient pas des catches communs.

**Limite de preuve de ce périmètre.** Lecture statique uniquement ; aucune exécution du projet ni mesure de coût.

<a id="finding-exa-evaluation-silencieuse"></a>

### EXA-EVALUATION-SILENCIEUSE — Une évaluation impossible disparaît de la sortie avec un code de succès

**P2 · défaut · confiance forte.** Périmètres : EXA-002.

**Mécanisme identifié.** interpret_evaluations ne conserve que les valeurs présentes ; eval_file saute ensuite toute évaluation absente et retourne kClean. Le contrôle statique des diviseurs ne connaît pas les paramètres liés à l’exécution.

**Condition à vérifier.** Module contenant def f(x): 1/x; puis f(0);. Le validateur voit x non lié et ne prouve pas zéro ; l’interpréteur lie x à0 et retourne nullopt.

**Impact.** Aucune ligne de résultat ni erreur pour une évaluation demandée ; un pipeline reçoit0 malgré l’absence de valeur.

**Direction de correction.** Conserver un résultat structuré pour chaque évaluation, expliquer nullopt et retourner un statut non nul si une évaluation échoue.

**Sources à relire.**

- [examples/arithmetics/src/core/arithmetics/Interpreter.cpp:91](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/arithmetics/src/core/arithmetics/Interpreter.cpp#L91) — Zéro dynamique devient nullopt.
- [examples/arithmetics/src/core/arithmetics/Interpreter.cpp:124](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/arithmetics/src/core/arithmetics/Interpreter.cpp#L124) — Les évaluations sans valeur sont omises.
- [examples/arithmetics/src/cli/arithmetics/Evaluator.cpp:35](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/arithmetics/src/cli/arithmetics/Evaluator.cpp#L35) — Absence ignorée et kClean à44.
- [examples/arithmetics/src/core/arithmetics/validation/ArithmeticsValidator.cpp:177](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/arithmetics/src/core/arithmetics/validation/ArithmeticsValidator.cpp#L177) — Évaluation du diviseur sans contexte de paramètres.

**Contexte EXA-002 — Évaluation numérique, liaison des arguments et fin d’évaluation.** Les arguments sont évalués dans le contexte appelant et le corps dans un contexte neuf indexé par identité de paramètre ; aucune fuite par homonymie n’est visible. Une référence absente, une arité incorrecte, un cycle actif ou un budget épuisé retourne nullopt. Le compteur partagé facture une unité par expression et suit les appels. Les opérations utilisent double, avec garde spécifique pour division/modulo par zéro ; pow peut retourner un non-fini. La projection CLI perd la raison et même la présence des échecs. L’évaluation CLI est sans budget et conserve des parcours récursifs.

**Limite de preuve de ce périmètre.** Lecture statique uniquement ; aucune exécution du projet ni mesure de coût.

<a id="finding-exa-generation-ecriture"></a>

### EXA-GENERATION-ECRITURE — Les trois générateurs annoncent la réussite sans vérifier l’écriture finale

**P2 · défaut · confiance forte.** Périmètres : EXA-013, EXA-030, EXA-031.

**Mécanisme identifié.** Chaque générateur ouvre un ofstream, vérifie is_open puis écrit sans activer les exceptions du flux ni vérifier flush/close. Les erreurs d’écriture différées ne deviennent pas des exceptions et les fonctions rendent quand même le chemin.

**Condition à vérifier.** Disque plein, quota atteint ou autre erreur après l’ouverture réussie du fichier Java, C++ ou HTML.

**Impact.** Fichier incomplet/tronqué accompagné d’un retour de succès. En Java plusieurs fichiers peuvent déjà avoir été publiés.

**Direction de correction.** Mutualiser un writer qui vérifie écriture, flush et close, et publier par remplacement d’un fichier temporaire après succès.

**Sources à relire.**

- [examples/domainmodel/src/cli/domainmodel/Generator.cpp:149](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/domainmodel/src/cli/domainmodel/Generator.cpp#L149) — Ouverture seule contrôlée ; écriture à154.
- [examples/statemachine/src/cli/statemachine/Generator.cpp:236](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/statemachine/src/cli/statemachine/Generator.cpp#L236) — Même séquence C++.
- [examples/requirements/src/cli/requirements/Generator.cpp:105](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/requirements/src/cli/requirements/Generator.cpp#L105) — Même séquence HTML.

**Contexte EXA-013 — Correspondance packages Java et arborescence des fichiers.** Un package pointé devient une arborescence et les packages imbriqués sont concaténés ; sous séparateurs /, la déclaration package et les répertoires concordent à partir du nom de fichier racine. Ce dernier n’est pas un identifiant Java garanti, et la conversion du chemin natif n’est pas portable. Les fichiers sont ouverts en troncature sans détection des collisions de types/noms de stem ; a-b et ab convergent. Le contrôle final d’écriture est absent dans les trois générateurs. Les commandes spécialisées ne bénéficient pas des catches communs.

**Limite de preuve de ce périmètre.** Lecture statique uniquement ; aucune exécution du projet ni mesure de coût.

**Contexte EXA-030 — Choix et publication du fichier C++ généré.** La destination par défaut est generated ; le nom de sortie est le stem privé de points/tirets avec fallback generated, puis .cpp. Le chemin peut converger entre plusieurs sources et l’ouverture tronque un fichier déjà présent. La génération est calculée après l’ouverture, donc une exception de contenu peut laisser une ancienne sortie tronquée. Les create_directories/open peuvent échouer ; seule l’ouverture est contrôlée et le chemin est retourné sans flush/close vérifié. Les mêmes choix de nom, écriture et gestion des exceptions sont dupliqués dans les trois exemples.

**Limite de preuve de ce périmètre.** Lecture statique uniquement ; aucune exécution du projet ni mesure de coût.

**Contexte EXA-031 — Choix et publication du rapport HTML.** Le rapport est placé sous la destination explicite ou generated et porte le stem sans points/tirets, suivi de .html ; un stem vidé devient generated. Cette normalisation fait converger a-b.req et ab.req et l’ouverture remplace une sortie existante. create_directories peut lever ; l’ouverture seule est contrôlée, pas la publication effective du HTML. Les erreurs de génération échappent au handler CLI commun et les erreurs de validation du périmètre n’empêchent pas le succès. Les helpers de destination/normalisation et la séquence d’écriture sont dupliqués avec domainmodel/statemachine.

**Limite de preuve de ce périmètre.** Lecture statique uniquement ; aucune exécution du projet ni mesure de coût.

<a id="finding-exa-heritage-revalidation"></a>

### EXA-HERITAGE-REVALIDATION — Un cycle d’héritage introduit dans un ancêtre éloigné ne revalide pas tous ses descendants

**P2 · défaut · confiance forte.** Périmètres : EXA-010.

**Mécanisme identifié.** Le validateur traverse plusieurs documents pour déterminer si une entité mène à un cycle. Le builder invalide les documents qui référencent directement les identités modifiées ; une simple remise à ComputedScopes d’un dépendant ne propage pas son identité dans changed. Aucune invalidation métier transitive n’est installée par DomainModel.

**Condition à vérifier.** A(doc1)→B(doc2)→C(doc3), tous validés ; modifier doc3 de C sans supertype vers C extends C en gardant les noms exportés. B est relinké/revalidé, A reste Validated alors que son check lirait maintenant un cycle.

**Impact.** Diagnostic de cycle absent ou périmé dans les descendants distants, jusqu’à une modification ou revalidation qui les inclut. Le cache local de sorties n’est pas la cause primaire.

**Direction de correction.** Déclarer les dépendances transitives de la validation d’héritage ou invalider les diagnostics de tous les descendants concernés quand une hiérarchie change.

**Sources à relire.**

- [examples/domainmodel/src/core/domainmodel/validation/DomainModelValidator.cpp:98](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/domainmodel/src/core/domainmodel/validation/DomainModelValidator.cpp#L98) — Le jugement parcourt les frontières de documents.
- [examples/domainmodel/src/core/domainmodel/validation/DomainModelValidator.hpp:47](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/domainmodel/src/core/domainmodel/validation/DomainModelValidator.hpp#L47) — Cache limité à la sortie locale, invalidé à Linked.
- [examples/domainmodel/src/core/domainmodel/CoreModule.cpp:30](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/domainmodel/src/core/domainmodel/CoreModule.cpp#L30) — Aucun hook d’invalidation métier.
- [src/core/cursiv/workspace/DefaultDocumentBuilder.cpp:552](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultDocumentBuilder.cpp#L552) — Invalidation des dépendants directs.
- [src/core/cursiv/workspace/DefaultDocumentBuilder.cpp:317](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultDocumentBuilder.cpp#L317) — changed ne comprend pas les simples relinks.
- [src/core/cursiv/workspace/DefaultIndexManager.cpp:271](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultIndexManager.cpp#L271) — isAffected compare les cibles directes.

**Contexte EXA-010 — Détection des cycles d’héritage entre documents.** exitOf parcourt itérativement la chaîne dans un seul document, distingue terminaison, frontière et cycle, et partage la sortie avec chaque entité rencontrée. Passed passe d’une petite liste à un set au seuil32. Le cache à Linked évite de garder un pointeur cible après relink du document source ; ne pas mémoïser toute la chaîne protège des changements distants lors d’une nouvelle validation. checkEntityDoesNotExtendItself détecte ensuite les réentrées entre documents. Toutefois une modification distante ne force pas cette nouvelle validation des descendants transitifs. Une chaîne répartie sur D documents est retraversée par chaque descendant, soit un coût global pouvant rester quadratique.

**Limite de preuve de ce périmètre.** Chemin d’invalidation du builder relu statiquement et confirmé en relecture croisée ; aucun scénario exécuté.

<a id="finding-exa-java-identites"></a>

### EXA-JAVA-IDENTITES — Les types Java émis ne suivent pas l’identité résolue du modèle

**P2 · défaut · confiance forte.** Périmètres : EXA-012, EXA-013.

**Mécanisme identifié.** Le générateur privilégie refText, sans normaliser la casse ni reconstruire le package Java de la cible. Il ajoute le nom de fichier comme package racine, ne génère que les entités, ne crée ni imports ni mapping des datatypes, et émet des classes de visibilité package.

**Condition à vérifier.** Référence StrinG résolue vers String ; référence DSL p.E produite sous le package <fichier>.p ; ou référence à une entité d’un package parent depuis un sous-package.

**Impact.** Modèles liés sans erreur transformés en types Java inconnus/inaccessibles ou pointant vers une autre identité. Les datatypes autres que les types Java déjà visibles demandent aussi un mapping absent.

**Direction de correction.** Construire les noms Java depuis les cibles résolues avec une politique explicite de packages/datatype/imports et de visibilité publique des classes interpackages.

**Sources à relire.**

- [examples/domainmodel/src/core/domainmodel/references/DomainModelScopeProvider.hpp:15](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/domainmodel/src/core/domainmodel/references/DomainModelScopeProvider.hpp#L15) — Résolution sans casse.
- [examples/domainmodel/src/cli/domainmodel/Generator.cpp:53](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/domainmodel/src/cli/domainmodel/Generator.cpp#L53) — Préférence au texte écrit.
- [examples/domainmodel/src/cli/domainmodel/Generator.cpp:65](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/domainmodel/src/cli/domainmodel/Generator.cpp#L65) — Même politique pour supertype.
- [examples/domainmodel/src/cli/domainmodel/Generator.cpp:106](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/domainmodel/src/cli/domainmodel/Generator.cpp#L106) — Package calculé et classe non publique.
- [examples/domainmodel/src/cli/domainmodel/Generator.cpp:146](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/domainmodel/src/cli/domainmodel/Generator.cpp#L146) — Seules les entités sont générées.
- [examples/domainmodel/src/cli/domainmodel/Generator.cpp:167](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/domainmodel/src/cli/domainmodel/Generator.cpp#L167) — Nom de fichier ajouté à la racine.

**Contexte EXA-012 — Projection des entités en classes et types Java.** La multiplicité ajoute [] au type et chaque feature non nulle obtient un champ privé, un setter et un getter. Le supertype optionnel est reconnu par texte absent. Cette projection conserve des problèmes d’identité : textes de références insensibles à la casse réémis tels quels, packages racines ajoutés sans adapter les références, classes non publiques et datatypes non projetés. Les conventions d’identifiants Java et les collisions d’accesseurs ne sont pas contrôlées.

**Limite de preuve de ce périmètre.** Lecture statique uniquement ; aucune exécution du projet ni mesure de coût.

**Contexte EXA-013 — Correspondance packages Java et arborescence des fichiers.** Un package pointé devient une arborescence et les packages imbriqués sont concaténés ; sous séparateurs /, la déclaration package et les répertoires concordent à partir du nom de fichier racine. Ce dernier n’est pas un identifiant Java garanti, et la conversion du chemin natif n’est pas portable. Les fichiers sont ouverts en troncature sans détection des collisions de types/noms de stem ; a-b et ab convergent. Le contrôle final d’écriture est absent dans les trois générateurs. Les commandes spécialisées ne bénéficient pas des catches communs.

**Limite de preuve de ce périmètre.** Lecture statique uniquement ; aucune exécution du projet ni mesure de coût.

<a id="finding-exa-java-noms"></a>

### EXA-JAVA-NOMS — Les noms et collisions admissibles dans le modèle peuvent produire du Java invalide

**P2 · défaut · confiance forte.** Périmètres : EXA-012.

**Mécanisme identifié.** Les identifiants DSL sont réémis dans les déclarations de classe, champs et accesseurs. La validation ne contrôle que la capitale initiale et les cycles ; elle n’interdit ni mots réservés Java ni doublons de features ni collisions après upper_first.

**Condition à vérifier.** Feature class: String ; deux features x ; ou features foo et Foo de même type, qui deviennent toutes deux getFoo()/setFoo(...).

**Impact.** Source Java non compilable malgré l’absence d’erreur de validation de génération.

**Direction de correction.** Valider l’ensemble des noms cibles et collisions avant écriture, ou appliquer un encodage bijectif documenté.

**Sources à relire.**

- [examples/domainmodel/src/core/domainmodel/DomainModelGrammar.hpp:25](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/domainmodel/src/core/domainmodel/DomainModelGrammar.hpp#L25) — Identifiants ASCII larges.
- [examples/domainmodel/src/core/domainmodel/CoreModule.cpp:30](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/domainmodel/src/core/domainmodel/CoreModule.cpp#L30) — Contrôles installés.
- [examples/domainmodel/src/cli/domainmodel/Generator.cpp:83](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/domainmodel/src/cli/domainmodel/Generator.cpp#L83) — Noms d’accesseurs upper_first.
- [examples/domainmodel/src/cli/domainmodel/Generator.cpp:108](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/domainmodel/src/cli/domainmodel/Generator.cpp#L108) — Nom de classe brut.
- [examples/domainmodel/src/cli/domainmodel/Generator.cpp:114](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/domainmodel/src/cli/domainmodel/Generator.cpp#L114) — Noms de champs bruts.

**Contexte EXA-012 — Projection des entités en classes et types Java.** La multiplicité ajoute [] au type et chaque feature non nulle obtient un champ privé, un setter et un getter. Le supertype optionnel est reconnu par texte absent. Cette projection conserve des problèmes d’identité : textes de références insensibles à la casse réémis tels quels, packages racines ajoutés sans adapter les références, classes non publiques et datatypes non projetés. Les conventions d’identifiants Java et les collisions d’accesseurs ne sont pas contrôlées.

**Limite de preuve de ce périmètre.** Lecture statique uniquement ; aucune exécution du projet ni mesure de coût.

<a id="finding-exa-java-package-chemin"></a>

### EXA-JAVA-PACKAGE-CHEMIN — Le package Java dérive d’un chemin insuffisamment normalisé

**P2 · défaut · confiance forte.** Périmètres : EXA-013.

**Mécanisme identifié.** Le nom racine provient du stem du fichier après suppression des seuls points et tirets ; espaces et chiffres initiaux restent permis par le filesystem mais interdits dans un segment package Java. Le package contenu remplace uniquement / par., alors que la composition utilise filesystem::path::string, qui peut rendre des séparateurs natifs Windows.

**Condition à vérifier.** Fichier source nommé 1 modèle.dmodel ; ou génération sous Windows d’un package imbriqué dont path::string comporte des antislashs.

**Impact.** Déclaration package Java invalide bien que la destination filesystem soit créée.

**Direction de correction.** Séparer la liste de segments Java du chemin natif, valider/encoder chaque segment et utiliser generic_string seulement pour les représentations indépendantes de plateforme.

**Sources à relire.**

- [examples/domainmodel/src/cli/domainmodel/CliUtils.cpp:11](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/domainmodel/src/cli/domainmodel/CliUtils.cpp#L11) — Sanitization limitée à . et -.
- [examples/domainmodel/src/cli/domainmodel/Generator.cpp:31](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/domainmodel/src/cli/domainmodel/Generator.cpp#L31) — Remplacement du seul slash.
- [examples/domainmodel/src/cli/domainmodel/Generator.cpp:144](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/domainmodel/src/cli/domainmodel/Generator.cpp#L144) — Composition via chemin natif.
- [examples/domainmodel/src/cli/domainmodel/Generator.cpp:106](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/domainmodel/src/cli/domainmodel/Generator.cpp#L106) — Insertion dans package.

**Contexte EXA-013 — Correspondance packages Java et arborescence des fichiers.** Un package pointé devient une arborescence et les packages imbriqués sont concaténés ; sous séparateurs /, la déclaration package et les répertoires concordent à partir du nom de fichier racine. Ce dernier n’est pas un identifiant Java garanti, et la conversion du chemin natif n’est pas portable. Les fichiers sont ouverts en troncature sans détection des collisions de types/noms de stem ; a-b et ab convergent. Le contrôle final d’écriture est absent dans les trois générateurs. Les commandes spécialisées ne bénéficient pas des catches communs.

**Limite de preuve de ce périmètre.** Lecture statique uniquement ; aucune exécution du projet ni mesure de coût.

<a id="finding-exa-normalisation-syntaxe"></a>

### EXA-NORMALISATION-SYNTAXE — La normalisation émet des nombres que la grammaire ne peut pas relire

**P2 · défaut · confiance forte.** Périmètres : EXA-006.

**Mécanisme identifié.** fold accepte tout double fini, y compris négatif ou formaté avec exposant. Le code action insère std::format("{}",value), alors que NUMBER ne contient ni signe ni exposant et que PrimaryExpression ne possède pas d’opérateur unaire.

**Condition à vérifier.** def a: 1-2; : le diagnostic propose-1 et l’édition remplace l’expression par-1. Les très petites/grandes valeurs peuvent aussi être écrites avec e.

**Impact.** Une action rapide offerte pour une expression valide introduit une erreur syntaxique.

**Direction de correction.** Définir un sérialiseur d’expression conforme à la grammaire : forme parenthésée de soustraction pour les négatifs et représentation décimale valide, ou étendre explicitement le langage.

**Sources à relire.**

- [examples/arithmetics/src/core/arithmetics/validation/ArithmeticsValidator.cpp:145](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/arithmetics/src/core/arithmetics/validation/ArithmeticsValidator.cpp#L145) — Tout résultat fini est conservé.
- [examples/arithmetics/src/lsp/arithmetics/ArithmeticsCodeActionProvider.cpp:46](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/arithmetics/src/lsp/arithmetics/ArithmeticsCodeActionProvider.cpp#L46) — Réémission générique du double.
- [examples/arithmetics/src/core/arithmetics/ArithmeticsGrammar.hpp:23](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/arithmetics/src/core/arithmetics/ArithmeticsGrammar.hpp#L23) — NUMBER sans signe/exposant.
- [examples/arithmetics/src/core/arithmetics/ArithmeticsGrammar.hpp:88](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/arithmetics/src/core/arithmetics/ArithmeticsGrammar.hpp#L88) — Pas de moins unaire.

**Contexte EXA-006 — Normalisation constante jusqu’à l’édition de remplacement.** Le fold est ascendant et retire les résultats enfants lorsqu’un parent peut les remplacer ; les valeurs non finies et divisions fautives sont exclues. Les informations de normalisation portent une donnée numérique structurée ; l’action utilise les diagnostics courants du document, filtre code, intersection de plage et type de donnée. Les appels ne sont pas traversés par fold, ce qui limite les suggestions aux sous-expressions binaires/groupées des corps de définitions. La réémission du résultat ignore cependant la syntaxe de NUMBER et casse le contrat de transformation.

**Limite de preuve de ce périmètre.** Lecture statique uniquement ; aucune exécution du projet ni mesure de coût.

<a id="finding-exa-parametre-callable"></a>

### EXA-PARAMETRE-CALLABLE — Un paramètre peut être appelé avec des arguments ignorés

**P2 · défaut · confiance forte.** Périmètres : EXA-002, EXA-028.

**Mécanisme identifié.** La grammaire autorise une liste d’arguments pour toute référence AbstractDefinition. checkMatchingParameters ne traite que Definition et retourne pour DeclaredParameter. evaluate_call renvoie immédiatement la valeur liée du paramètre, sans vérifier ni évaluer ses arguments.

**Condition à vérifier.** def f(x): x(2); f(3); est syntaxiquement admissible, sans erreur d’arité sur x, et l’évaluateur renvoie3.

**Impact.** Appel de valeur numérique accepté ; les arguments écrits ne participent pas au calcul, donnant un résultat trompeur.

**Direction de correction.** Interdire explicitement les arguments sur DeclaredParameter et appliquer la même garde dans l’interpréteur.

**Sources à relire.**

- [examples/arithmetics/src/core/arithmetics/ArithmeticsGrammar.hpp:95](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/arithmetics/src/core/arithmetics/ArithmeticsGrammar.hpp#L95) — Arguments permis pour une cible de la base commune.
- [examples/arithmetics/src/core/arithmetics/validation/ArithmeticsValidator.cpp:24](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/arithmetics/src/core/arithmetics/validation/ArithmeticsValidator.cpp#L24) — definition_of ignore DeclaredParameter.
- [examples/arithmetics/src/core/arithmetics/validation/ArithmeticsValidator.cpp:272](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/arithmetics/src/core/arithmetics/validation/ArithmeticsValidator.cpp#L272) — Le contrôle d’arité quitte pour cette cible.
- [examples/arithmetics/src/core/arithmetics/Interpreter.cpp:37](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/arithmetics/src/core/arithmetics/Interpreter.cpp#L37) — Valeur liée rendue avant traitement des arguments.

**Contexte EXA-002 — Évaluation numérique, liaison des arguments et fin d’évaluation.** Les arguments sont évalués dans le contexte appelant et le corps dans un contexte neuf indexé par identité de paramètre ; aucune fuite par homonymie n’est visible. Une référence absente, une arité incorrecte, un cycle actif ou un budget épuisé retourne nullopt. Le compteur partagé facture une unité par expression et suit les appels. Les opérations utilisent double, avec garde spécifique pour division/modulo par zéro ; pow peut retourner un non-fini. La projection CLI perd la raison et même la présence des échecs. L’évaluation CLI est sans budget et conserve des parcours récursifs.

**Limite de preuve de ce périmètre.** Lecture statique uniquement ; aucune exécution du projet ni mesure de coût.

**Contexte EXA-028 — Arité des appels et nature de leur cible.** Les définitions résolues sont contrôlées par comparaison exacte du nombre d’arguments ; le diagnostic indique nom, quantité attendue et quantité reçue. Une référence non résolue reste à la charge du linker. La seconde nature de cible autorisée, DeclaredParameter, n’est pas contrôlée, bien qu’une liste d’arguments reste grammaticalement possible. Le comportement de l’interpréteur confirme qu’ils sont alors ignorés.

**Limite de preuve de ce périmètre.** Lecture statique uniquement ; aucune exécution du projet ni mesure de coût.

<a id="finding-exa-req-chaines-langium"></a>

### EXA-REQ-CHAINES-LANGIUM — Les chaînes de requirements conservent leurs échappements, contrairement à Langium

**P2 · écart d’API · confiance forte.** Périmètres : EXA-015.

**Mécanisme identifié.** Le terminal STRING accepte un antislash suivi d’un caractère, mais son converter unquote enlève seulement les deux délimiteurs. Le ValueConverter STRING de Langium interprète les échappements usuels et enlève l’antislash pour les autres. Cursiv exclut aussi les retours de ligne non échappés que le terminal commun de l’exemple Langium admet.

**Condition à vérifier.** Texte de contact/exigence ou testfile avec un guillemet échappé, \n, \t ou \\.

**Impact.** Les propriétés AST de l’exemple Cursiv portent une valeur différente pour le même texte source ; les consommateurs de chaînes/path ne sont pas directement compatibles.

**Direction de correction.** Documenter la divergence ou fournir un décodage équivalent avec stockage propriétaire dans l’arène, et préciser la syntaxe des retours de ligne.

**Sources à relire.**

- [examples/requirements/src/core/requirements/RequirementsGrammar.hpp:10](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/requirements/src/core/requirements/RequirementsGrammar.hpp#L10) — Converter réduit à substr.
- [examples/requirements/src/core/requirements/RequirementsGrammar.hpp:41](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/requirements/src/core/requirements/RequirementsGrammar.hpp#L41) — STRING accepte les échappements et exclut CR/LF bruts.
- [langium/packages/langium/src/parser/value-converter.ts:60](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/parser/value-converter.ts#L60) — Décodage STRING Langium.
- [langium/packages/langium/src/parser/value-converter.ts:74](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/parser/value-converter.ts#L74) — Table des échappements.
- [langium/examples/requirements/src/language-server/common.langium:6](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/examples/requirements/src/language-server/common.langium#L6) — Terminal STRING de référence.

**Contexte EXA-015 — Contrat commun des langages d’exigences et de tests.** Deux entrées héritent du même jeu Rules et utilisent les mêmes classes AST, donc Requirement et Environment ont une identité de type commune entre .req et .tst. La création et l’enregistrement installent les deux services dans le même workspace avant retour. Contact est optionnel, les environnements précèdent les exigences, chaque modèle possède au moins un élément principal, et chaque liste présente est non vide. Les champs testFile et environnements représentent explicitement les clauses absentes. Les chaînes ne sont cependant pas décodées comme dans l’exemple Langium.

**Limite de preuve de ce périmètre.** Lecture statique uniquement ; aucune exécution du projet ni mesure de coût.

<a id="finding-exa-req-couverture-perimee"></a>

### EXA-REQ-COUVERTURE-PERIMEE — Modifier les tests ne rafraîchit pas les diagnostics de couverture des exigences

**P2 · défaut · confiance forte.** Périmètres : EXA-017.

**Mécanisme identifié.** computeCoverage lit les références Test→Requirement de tout le workspace ; les deux caches sont effacés à l’annonce de build, mais aucun hook ne réinscrit les documents Requirement déjà Validated. L’invalidation générique suit les références sortantes du document affecté, pas les lecteurs métier de la table inverse.

**Condition à vérifier.** Valider req R1 sans test, puis créer un fichier tst T1 tests R1. Le .req ne référence pas le .tst et peut rester Validated ; cas symétrique lors de suppression ou changement du dernier test couvrant R1.

**Impact.** Warning de non-couverture conservé à tort, ou warning manquant après perte de couverture, jusqu’à une revalidation du .req. Vider le cache ne suffit pas à republier les diagnostics.

**Direction de correction.** Sur changement de références de tests, identifier les exigences dont la couverture change et réinscrire leurs documents en validation, ou enregistrer explicitement cette dépendance inverse.

**Sources à relire.**

- [examples/requirements/src/core/requirements/validation/RequirementsValidator.cpp:43](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/requirements/src/core/requirements/validation/RequirementsValidator.cpp#L43) — Couverture calculée depuis tout le workspace.
- [examples/requirements/src/core/requirements/validation/RequirementsValidator.cpp:61](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/requirements/src/core/requirements/validation/RequirementsValidator.cpp#L61) — Consultation seulement lors du check Requirement.
- [examples/requirements/src/core/requirements/validation/RequirementsValidator.hpp:40](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/requirements/src/core/requirements/validation/RequirementsValidator.hpp#L40) — Caches sans politique de revalidation.
- [examples/requirements/src/core/requirements/CoreModule.cpp:30](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/requirements/src/core/requirements/CoreModule.cpp#L30) — Aucun hook d’invalidation métier.
- [src/core/cursiv/utils/WorkspaceCache.hpp:49](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/utils/WorkspaceCache.hpp#L49) — Effacement du cache seul.
- [src/core/cursiv/workspace/DefaultDocumentBuilder.cpp:539](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultDocumentBuilder.cpp#L539) — Un document pleinement validé reste hors work.
- [src/core/cursiv/workspace/DefaultIndexManager.cpp:271](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultIndexManager.cpp#L271) — Dépendances de références directes.

**Contexte EXA-017 — Couverture inverse d’une exigence par les tests du workspace.** Le calcul parcourt tous les TestModel stockés et insère l’identité des Requirement effectivement résolues dans un set ; doublons de références ou tests n’influent pas sur le booléen couvert. Un shared_ptr propriétaire et un cache de pointeur brut évitent un comptage partagé par exigence ; un mutex sérialise la construction. Les deux caches sont vidés à chaque annonce de build, donc une nouvelle passe lit le nouvel état. Il manque cependant le déclenchement de cette nouvelle passe pour les exigences quand seuls leurs tests changent. Le coût du scan est une passe sur les références de tests et les recherches par exigence sont ensuite amorties constantes.

**Limite de preuve de ce périmètre.** Chemin d’invalidation relu et confirmé en relecture croisée ; aucun scénario exécuté ni mesure parallèle.

<a id="finding-exa-req-rapport-erreurs"></a>

### EXA-REQ-RAPPORT-ERREURS — Le rapport est annoncé réussi malgré des erreurs de modèle ou de tests

**P2 · défaut · confiance forte.** Périmètres : EXA-019, EXA-031.

**Mécanisme identifié.** La commande requirements generate construit le document mais ne consulte ni ses diagnostics ni ceux des TestModel récupérés du workspace. Un AST récupéré ou partiellement lié suffit pour générer et retourner kClean.

**Condition à vérifier.** Une exigence avec un environnement non résolu, ou un .tst voisin dont la référence d’exigence est introuvable, tout en conservant une racine AST.

**Impact.** Rapport HTML incomplet présenté comme généré avec succès ; les références manquantes disparaissent des liens de couverture sans explication.

**Direction de correction.** Refuser les erreurs de parsing/linking sur les documents du rapport, ou produire explicitement un rapport partiel avec erreurs listées et un code de sortie distinct.

**Sources à relire.**

- [examples/requirements/src/cli.cpp:51](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/requirements/src/cli.cpp#L51) — Build sans contrôle de diagnostics.
- [examples/requirements/src/cli.cpp:54](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/requirements/src/cli.cpp#L54) — Seule présence/type AST vérifiée.
- [examples/requirements/src/cli.cpp:63](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/requirements/src/cli.cpp#L63) — Tous les TestModel sont retenus.
- [examples/requirements/src/cli.cpp:72](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/requirements/src/cli.cpp#L72) — Succès inconditionnel après génération.
- [examples/requirements/src/cli/requirements/Generator.cpp:77](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/requirements/src/cli/requirements/Generator.cpp#L77) — Références non résolues ne couvrent rien.

**Contexte EXA-019 — Périmètre et contenu du rapport HTML de couverture.** La commande scanne le dossier source et rassemble tous les TestModel du store ; le générateur produit une ligne par exigence du modèle demandé et un bloc par test dont une référence résolue vise exactement cette exigence. Une référence répétée dans un test ne le duplique pas, grâce à any_of ; sans couverture la cellule reste vide. Les chemins libres sont échappés pour le contexte texte HTML (&,<,>) ; les noms proviennent d’ID et ne nécessitent pas d’échappement supplémentaire pour les modèles syntaxiquement valides. Les div sont correctement fermés, contrairement au template Langium lu. Le coût répète le scan pour chaque exigence et la commande ne refuse pas les documents en erreur.

**Limite de preuve de ce périmètre.** Lecture statique uniquement ; aucune exécution du projet ni mesure de coût.

**Contexte EXA-031 — Choix et publication du rapport HTML.** Le rapport est placé sous la destination explicite ou generated et porte le stem sans points/tirets, suivi de .html ; un stem vidé devient generated. Cette normalisation fait converger a-b.req et ab.req et l’ouverture remplace une sortie existante. create_directories peut lever ; l’ouverture seule est contrôlée, pas la publication effective du HTML. Les erreurs de génération échappent au handler CLI commun et les erreurs de validation du périmètre n’empêchent pas le succès. Les helpers de destination/normalisation et la séquence d’écriture sont dupliqués avec domainmodel/statemachine.

**Limite de preuve de ce périmètre.** Lecture statique uniquement ; aucune exécution du projet ni mesure de coût.

<a id="finding-exa-sm-actions-omises"></a>

### EXA-SM-ACTIONS-OMISES — Les commandes et actions du modèle disparaissent du C++ généré

**P2 · limite · confiance forte.** Périmètres : EXA-025.

**Mécanisme identifié.** La grammaire documente les commands comme exécutables par les états et actions comme la liste des commandes que l’état lance. Le générateur ne parcourt jamais model.commands ni state.actions ; ni le constructeur ni les transitions n’émettent de callback ou représentation de ces actions.

**Condition à vérifier.** Exemple fourni Doors : Closed actions { lock }, Open actions { unlock }.

**Impact.** Le programme généré change d’état mais ne représente ni ne déclenche lock/unlock ; l’ordre et la multiplicité des actions n’ont aucun effet.

**Direction de correction.** Définir explicitement la sémantique d’entrée/sortie des actions et générer leur point d’extension ; sinon annoncer cette partie de la grammaire comme non prise en charge par le générateur.

**Sources à relire.**

- [examples/statemachine/src/core/statemachine/StateMachineGrammar.hpp:20](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/statemachine/src/core/statemachine/StateMachineGrammar.hpp#L20) — Contrat commands/actions dans la documentation des mots-clés.
- [examples/statemachine/src/core/statemachine/StateMachineGrammar.hpp:38](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/statemachine/src/core/statemachine/StateMachineGrammar.hpp#L38) — Liste d’actions admise.
- [examples/statemachine/example/doors.statemachine:16](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/statemachine/example/doors.statemachine#L16) — Cas fourni avec lock/unlock.
- [examples/statemachine/src/cli/statemachine/Generator.cpp:97](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/statemachine/src/cli/statemachine/Generator.cpp#L97) — Initialisation sans actions.
- [examples/statemachine/src/cli/statemachine/Generator.cpp:109](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/statemachine/src/cli/statemachine/Generator.cpp#L109) — Transition sans actions.
- [examples/statemachine/src/cli/statemachine/Generator.cpp:212](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/statemachine/src/cli/statemachine/Generator.cpp#L212) — Toutes les passes de génération omettent commandes/actions.
- [langium/examples/statemachine/src/cli/generator.ts:50](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/examples/statemachine/src/cli/generator.ts#L50) — Même périmètre restreint dans le générateur de référence.

**Contexte EXA-025 — Sémantique des commandes et actions associées aux états.** Les actions sont des références ordonnées vers Command et peuvent être répétées grammaticalement ; la validation les résout et le formatter les présente dans leur ordre. Aucune des passes du générateur n’en consomme une : seules les classes d’états, événements, transitions et main sont émises. Il manque donc une sémantique observable des commandes à l’état initial comme à l’entrée après transition ou boucle sur soi. Ce manque est celui des exemples de génération Cursiv et Langium, pas une absence du mécanisme générique de références du framework.

**Limite de preuve de ce périmètre.** Lecture statique uniquement ; aucune exécution du projet ni mesure de coût.

<a id="finding-exa-sm-copie-ownership"></a>

### EXA-SM-COPIE-OWNERSHIP — La machine générée est copiable malgré un état possédé par pointeur brut

**P2 · écart d’API · confiance forte.** Périmètres : EXA-024.

**Mécanisme identifié.** Le code généré définit un destructeur qui delete state, mais ne supprime ni ne redéfinit le constructeur de copie et l’affectation. La copie implicite recopie le pointeur State ; le contexte mémorisé dans cet état désigne toujours la machine d’origine.

**Condition à vérifier.** Réutilisation de la classe générée en C++ avec copie par valeur ou affectation ; le main généré lui-même ne copie pas.

**Impact.** Double destruction à la fin de vie de deux copies, transitions sur la mauvaise machine, et possible accès pendant à l’état après transition d’une copie.

**Direction de correction.** Utiliser unique_ptr, supprimer explicitement la copie, et définir les opérations de déplacement avec remise en cohérence du contexte si elles sont nécessaires.

**Sources à relire.**

- [examples/statemachine/src/cli/statemachine/Generator.cpp:90](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/statemachine/src/cli/statemachine/Generator.cpp#L90) — Classe avec pointeur possédé.
- [examples/statemachine/src/cli/statemachine/Generator.cpp:97](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/statemachine/src/cli/statemachine/Generator.cpp#L97) — Contexte lié à this.
- [examples/statemachine/src/cli/statemachine/Generator.cpp:103](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/statemachine/src/cli/statemachine/Generator.cpp#L103) — Destructeur delete sans politique de copie.
- [examples/statemachine/src/cli/statemachine/Generator.cpp:109](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/statemachine/src/cli/statemachine/Generator.cpp#L109) — Remplacement de l’état.
- [langium/examples/statemachine/src/cli/generator.ts:99](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/examples/statemachine/src/cli/generator.ts#L99) — Même squelette d’ownership dans l’exemple Langium.

**Contexte EXA-024 — Durée de vie des états et contexte de la machine générée.** Le constructeur lie le contexte avant d’activer l’état ; chaque transition crée un nouvel objet, y compris une boucle sur soi, puis détruit l’ancien et lie le nouveau contexte. Le destructeur virtuel de State permet la suppression dérivée. Le handler qui déclenche transition_to ne lit plus ses membres après cette invocation : la destruction de son objet pendant cet appel n’établit donc pas, à elle seule, un use-after-free dans le main généré. En revanche la classe possède un pointeur brut destructible et conserve les copies implicites, incompatibles avec cet ownership. Cette limite se retrouve dans le squelette Langium lu.

**Limite de preuve de ce périmètre.** Défaut d’API de réutilisation du C++ généré ; aucun crash démontré dans son main autonome ni exécution effectuée.

<a id="finding-exa-sm-noms-cpp"></a>

### EXA-SM-NOMS-CPP — Les noms acceptés ne sont pas protégés contre les collisions C++ générées

**P2 · défaut · confiance forte.** Périmètres : EXA-022.

**Mécanisme identifié.** Le validateur interdit les sept mots structurants DSL et les doublons exacts entre états/événements ; il ne contrôle ni les mots réservés C++, ni le nom de machine contre ses états, ni les méthodes auxiliaires générées. Les noms sont injectés directement dans les déclarations.

**Condition à vérifier.** Événement get_name : le State généré contient déjà virtual std::string get_name() et ajoute virtual void get_name(). Autres cas : événement delete, ou machine Foo contenant un état Foo.

**Impact.** C++ invalide pour des modèles sans erreur de validation ; un warning de capitale n’empêche pas generate.

**Direction de correction.** Réserver/encoder les noms cibles et vérifier toutes les collisions de symboles générés avant de publier le fichier.

**Sources à relire.**

- [examples/statemachine/src/core/statemachine/validation/StateMachineValidator.cpp:23](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/statemachine/src/core/statemachine/validation/StateMachineValidator.cpp#L23) — Unicité limitée aux états/événements.
- [examples/statemachine/src/core/statemachine/validation/StateMachineValidator.cpp:62](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/statemachine/src/core/statemachine/validation/StateMachineValidator.cpp#L62) — Mots réservés DSL seulement.
- [examples/statemachine/src/cli/statemachine/Generator.cpp:74](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/statemachine/src/cli/statemachine/Generator.cpp#L74) — get_name auxiliaire.
- [examples/statemachine/src/cli/statemachine/Generator.cpp:82](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/statemachine/src/cli/statemachine/Generator.cpp#L82) — Méthode d’événement brute.
- [examples/statemachine/src/cli/statemachine/Generator.cpp:91](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/statemachine/src/cli/statemachine/Generator.cpp#L91) — Nom de machine brut.
- [examples/statemachine/src/cli/statemachine/Generator.cpp:139](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/statemachine/src/cli/statemachine/Generator.cpp#L139) — Nom d’état brut.

**Contexte EXA-022 — Identité et admissibilité des noms de machine.** L’unicité des états et événements est déterministe : un seul tableau commun, première occurrence diagnostiquée lors du premier doublon, puis chaque occurrence supplémentaire. Les noms sont comparés exactement, tandis que les mots structurants sont refusés sans égard à la casse, cohérent avec ikw. Les commandes sont soumises aux mots réservés mais pas au contrôle d’unicité ; le nom de machine n’est pas comparé aux autres noms. La convention de capitale des états est un warning ASCII. Ces contrôles ne suffisent pas aux identifiants et symboles du C++ produit.

**Limite de preuve de ce périmètre.** Lecture statique uniquement ; aucune exécution du projet ni mesure de coût.

<a id="finding-exa-sm-references-externes"></a>

### EXA-SM-REFERENCES-EXTERNES — La génération accepte des cibles interdocuments qu’elle ne déclare pas

**P2 · défaut · confiance forte.** Périmètres : EXA-021, EXA-023, EXA-026.

**Mécanisme identifié.** La commande scanne les autres fichiers du dossier pour résoudre les références, mais generate_cpp_content ne reçoit que le Statemachine source et ne génère que ses vectors states/events. Une cible initiale ou de transition située dans un autre modèle peut être parfaitement résolue sans être émise ; un événement externe n’existe pas non plus dans la classe State de base ni dans le dispatch du main.

**Condition à vérifier.** Fichier M avec initialState Remote, aucun état local, et Remote déclaré dans un fichier voisin N ; ou une transition locale visant un événement/état du voisin.

**Impact.** C++ contenant new Remote sans déclaration/classe, ou override sans membre de base ; fichier généré non compilable malgré le linking réussi.

**Direction de correction.** Restreindre la validation de génération aux déclarations de la machine locale ou calculer la fermeture des états/événements à émettre, avec une règle explicite de composition des machines.

**Sources à relire.**

- [examples/statemachine/src/cli.cpp:53](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/statemachine/src/cli.cpp#L53) — Chargement du dossier pour les références interdocuments.
- [src/core/cursiv/references/DefaultScopeComputation.cpp:19](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/references/DefaultScopeComputation.cpp#L19) — Export des membres racine, donc états/événements.
- [examples/statemachine/src/core/statemachine/StateMachineGrammar.hpp:49](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/statemachine/src/core/statemachine/StateMachineGrammar.hpp#L49) — Zéro état local possible avec initialState résolu.
- [examples/statemachine/src/cli/statemachine/Generator.cpp:77](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/statemachine/src/cli/statemachine/Generator.cpp#L77) — Base limitée aux événements locaux.
- [examples/statemachine/src/cli/statemachine/Generator.cpp:132](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/statemachine/src/cli/statemachine/Generator.cpp#L132) — Déclarations limitées aux états locaux.
- [examples/statemachine/src/cli/statemachine/Generator.cpp:180](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/statemachine/src/cli/statemachine/Generator.cpp#L180) — InitialState résolu injecté sans fermeture.
- [examples/statemachine/src/cli/statemachine/Generator.cpp:161](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/statemachine/src/cli/statemachine/Generator.cpp#L161) — Cible de transition résolue injectée.

**Contexte EXA-021 — Contrat syntaxique des machines et références typées.** La grammaire impose le nom de machine et une référence initialState, mais permet zéro état local. Les sections events et commands sont optionnelles et non vides lorsqu’écrites. Un état a éventuellement une liste non vide d’actions puis zéro ou plusieurs transitions avant end. Transition.event, Transition.state, State.actions et Statemachine.init ont des cibles AST distinctes, respectivement Event, State, Command et State. Les erreurs de résolution sont remises au linker ; la présence d’un état initial local n’est pas garantie, ce qui devient important pour la génération d’un seul fichier.

**Limite de preuve de ce périmètre.** Lecture statique uniquement ; aucune exécution du projet ni mesure de coût.

**Contexte EXA-023 — Sélection d’une transition par événement et génération des handlers.** La sélection est explicitement première transition résolue par nom d’événement. Les nœuds nuls ou références absentes sont ignorés ; un doublon ultérieur ne génère donc pas une seconde méthode. Déclarations et définitions appellent le même helper avec le même AST, ce qui garantit leur accord. Un état sans transition hérite des handlers affichant Impossible event. La priorité du premier doublon est déterministe mais ne fait pas l’objet d’un diagnostic. Le vector des noms vus induit un coût quadratique, et les références interdocuments peuvent produire un override absent du State de base.

**Limite de preuve de ce périmètre.** Lecture statique uniquement ; aucune exécution du projet ni mesure de coût.

**Contexte EXA-026 — Programme autonome généré et contrat d’entrée-sortie.** write_main exige une référence initiale résolue, crée la machine et dispatch chaque ligne entière via map<string,pointeur de méthode>. La comparaison d’entrée est exacte ; les noms inconnus impriment un message et continuent. La boucle conditionnée par getline termine correctement à EOF, détruit la machine et retourne0. Les pointeurs d’événements proviennent du même vecteur que les wrappers machine. En revanche la phase CLI a volontairement rendu les cibles voisines visibles sans que la génération inclue leurs états/événements ; la précondition implicite de localité n’est ni validée ni respectée.

**Limite de preuve de ce périmètre.** Lecture statique uniquement ; aucune exécution du projet ni mesure de coût.

<a id="finding-host-mcp-listen-error"></a>

### HOST-MCP-LISTEN-ERROR — L’erreur d’écoute du bridge MCP n’a pas de gestionnaire

**P2 · défaut · confiance forte.** Périmètres : HOST-022.

**Mécanisme identifié.** Le serveur HTTP est créé puis listen est appelé dans une Promise résolue uniquement par le callback de succès. Aucun handler server.on(error) n’est installé ; une erreur asynchrone d’écoute n’est donc ni transformée en rejet de registration ni signalée proprement comme indisponibilité du bridge.

**Condition à vérifier.** Échec du bind/listen au démarrage de l’extension, par exemple refus système ou épuisement de ressources.

**Impact.** Événement error non traité dans l’extension host et enregistrement resté pendant ; la dégradation du bridge peut affecter l’activation de l’extension.

**Direction de correction.** Installer error avant listen, terminer la promesse sur tous les chemins et rattacher la fermeture au statut d’enregistrement ; journaliser une indisponibilité sans laisser de promesse pendante.

**Sources à relire.**

- [examples/domainmodel/vscode/src/mcp.ts:50](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/domainmodel/vscode/src/mcp.ts#L50) — Serveur sans handler error.
- [examples/domainmodel/vscode/src/mcp.ts:101](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/domainmodel/vscode/src/mcp.ts#L101) — Promise résolue seulement au succès.
- [examples/domainmodel/vscode/src/mcp.ts:129](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/domainmodel/vscode/src/mcp.ts#L129) — Dispose dépendant de cette promesse.

**Contexte HOST-022 — Transmission et fermeture du bridge MCP.** Chaque POST JSON est transmis comme demande de tunnel cursiv/mcp sur le même LanguageClient ; le retour null devient202 sans corps et les autres retours JSON deviennent200. Une erreur de tunnel reçoit l’ID de la demande quand il existe. L’API VSCode MCP est détectée dynamiquement pour supporter les versions antérieures ; après résolution, dispose enlève l’enregistrement et ferme le serveur. Aucun SSE n’est annoncé ni produit. Les chemins de transport et de fermeture sont identiques dans les exemples et le template.

**Limite de preuve de ce périmètre.** Une fermeture HTTP ou une annulation du client ne propage pas de token au sendRequest du tunnel ; aucun timeout applicatif ne borne une demande serveur muette. L’endpoint réel décide validité JSON-RPC et identités, audité dans MCP.

<a id="finding-host-web-completion-partial"></a>

### HOST-WEB-COMPLETION-PARTIAL — L’adaptateur Monaco omet les modifications annexes des complétions

**P2 · écart d’API · confiance forte.** Périmètres : HOST-015.

**Mécanisme identifié.** La conversion conserve label/kind/detail/documentation, insertion/remplacement, snippet, sortText/filterText et isIncomplete. Elle n’achemine ni additionalTextEdits, ni command, ni data/resolveCompletionItem. La requête perd aussi context de Monaco.

**Condition à vérifier.** Provider qui ajoute un import ou une autre modification avec la complétion, ou dont l’item doit être résolu.

**Impact.** Le texte principal est inséré mais l’opération sémantique complète peut rester inachevée ; les items nécessitant resolve ne sont pas enrichis.

**Direction de correction.** Adapter explicitement ces champs et le protocole resolve, ou limiter/documenter les capacités de ce client sans annoncer celles qu’il ne rend pas.

**Sources à relire.**

- [docs/demo/lsp.js:62](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/docs/demo/lsp.js#L62) — Requête sans contexte.
- [docs/demo/lsp.js:71](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/docs/demo/lsp.js#L71) — Champs de conversion retenus.

**Contexte HOST-015 — Adaptation de la complétion vers Monaco.** Le mapping des catégories s’appuie sur les noms d’enum Monaco, évitant de supposer le même index. Le fallback range est le mot jusqu’au caret ; les TextEdit et InsertReplaceEdit explicites prennent priorité. newText précède insertText puis label et le flag snippet est conservé. isIncomplete survit dans la liste. Les champs omis constituent une limite de fonction, distincte de la justesse de ces conversions de base.

**Limite de preuve de ce périmètre.** Le provider par défaut n’utilise pas nécessairement les champs annexes à chaque réponse ; le défaut devient observable avec les extensions correspondantes. Les triggers sont capturés une seule fois lors du premier enregistrement du languageId. Le client n’annonce pas contextSupport ; la perte de contexte est donc une restriction fonctionnelle et non une promesse de capacité violée.

<a id="finding-host-web-edit-version"></a>

### HOST-WEB-EDIT-VERSION — Les préconditions de version des WorkspaceEdit sont perdues

**P2 · défaut · confiance forte.** Périmètres : HOST-017.

**Mécanisme identifié.** workspaceEdit transforme chaque TextDocumentEdit avec versionId:undefined et ignore change.textDocument.version. La page annonce la variante documentChanges, mais ne reporte pas sa précondition de version au moteur d’édition Monaco.

**Condition à vérifier.** Réponse de rename/codeAction contenant un TextDocumentEdit versionné, suivie d’une édition locale avant son application.

**Impact.** Un edit périmé peut être appliqué sans la précondition de version fournie par le serveur.

**Direction de correction.** Reporter la version compatible Monaco et refuser une version incompatible. Les opérations de fichiers, non négociées par cette démo, doivent être refusées explicitement si un serveur en envoie tout de même.

**Sources à relire.**

- [docs/demo/lsp.js:27](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/docs/demo/lsp.js#L27) — Effacement des versions et filtrage des documentChanges.
- [docs/demo/app.js:257](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/docs/demo/app.js#L257) — Annonce documentChanges.

**Contexte HOST-017 — Conversion des modifications LSP vers Monaco.** Les URI et plages des modifications textuelles sont conservées, qu’elles viennent de changes ou de documentChanges. Les variantes prepareRename range/placeholder/default sont traitées et formatting convertit les options et les TextEdit. CodeAction ne garde que les actions contenant déjà edit ; commandes, résolution, disabled et contexte only ne sont pas acheminés. La perte de version est plus grave qu’une décoration manquante, car elle supprime une condition d’application de l’edit.

**Limite de preuve de ce périmètre.** L’annonce n’inclut pas resourceOperations ; le serveur courant doit respecter les capacités négociées. La perte de version affecte en revanche la variante documentChanges annoncée. Pas d’édition appliquée pendant l’audit.

<a id="finding-host-web-frame-failure"></a>

### HOST-WEB-FRAME-FAILURE — Une trame web invalide ne clôt pas les requêtes en attente

**P2 · défaut · confiance forte.** Périmètres : HOST-010.

**Mécanisme identifié.** feed recherche la fin des headers dans les256 premiers octets seulement, déréférence sans contrôle le match Content-Length et parse JSON sans catch. Une erreur sort du callback message ; stop n’est pas appelé. Un header complet au-delà de256 n’est jamais reconnu.

**Condition à vérifier.** Serveur émettant un header plus long que256 octets, un header sans longueur ou un corps JSON invalide.

**Impact.** La session peut rester running avec des promesses pendantes ; des chunks continuent d’être accumulés, sans diagnostic de transport structuré.

**Direction de correction.** Borner explicitement header et corps, valider Content-Length, capturer les erreurs de framing et arrêter/rejeter la session ; conserver un curseur pour éviter la recopie répétée de gros buffers incomplets.

**Sources à relire.**

- [docs/demo/server.js:74](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/docs/demo/server.js#L74) — Callback stdout vers feed.
- [docs/demo/server.js:96](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/docs/demo/server.js#L96) — Limite256, match et JSON.parse sans capture.
- [docs/demo/server.js:65](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/docs/demo/server.js#L65) — Chemin stop non emprunté par cette erreur.

**Contexte HOST-010 — Sortie Wasm et cadrage LSP dans la page.** writeOut copie toutes les iovecs dans un buffer possédé avant postMessage transférable ; aucune vue de mémoire Wasm mutable n’est livrée à la page. Les frames sont séparées par worker et le décodage UTF8 du corps attend Content-Length bytes. La concaténation de l’accumulateur à chaque chunk peut coûter quadratiquement pour une grande frame fragmentée. Les logs stderr sont décodés chunk par chunk sans option stream, ce qui peut remplacer un scalaire UTF8 coupé ; cela ne corrompt pas le canal LSP.

**Limite de preuve de ce périmètre.** La séparation par worker suppose que le serveur émet chaque message depuis un même thread ; pas de démonstration de mélange réel dans le writer C++ courant. Les headers courants courts restent sur le chemin nominal.

<a id="finding-host-web-poll-deadlines"></a>

### HOST-WEB-POLL-DEADLINES — poll_oneoff annonce aussi les horloges dont le délai n’est pas échu

**P2 · défaut · confiance forte.** Périmètres : HOST-008.

**Mécanisme identifié.** pollOneoff attend seulement le minimum des délais de ses abonnements, puis émet un événement de succès pour chaque abonnement clock sans recontrôler son échéance.

**Condition à vérifier.** Un appel poll_oneoff contient plusieurs abonnements clock d’échéances différentes.

**Impact.** Le client peut considérer une attente longue comme terminée dès l’expiration de la plus courte. Le cas usuel d’un unique sleep ne met pas ce défaut en évidence.

**Direction de correction.** Conserver les échéances par abonnement, attendre la première puis n’émettre que les événements devenus prêts ; traiter les types et IDs non supportés explicitement.

**Sources à relire.**

- [docs/demo/wasi-worker.js:159](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/docs/demo/wasi-worker.js#L159) — Calcul du minimum.
- [docs/demo/wasi-worker.js:173](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/docs/demo/wasi-worker.js#L173) — Émission de toutes les clocks sans test supplémentaire.

**Contexte HOST-008 — ABI des imports WASI auxiliaires du navigateur.** Les vues mémoire sont recréées depuis memory.buffer à chaque import. args/environ calculent leurs tailles en octets UTF8 avec NUL, puis écrivent les pointeurs32 correspondants. random_get découpe à64KiB et passe par un buffer privé, car getRandomValues refuse une vue partagée. Les offsets subscription48/event32 sont cohérents avec le sous-ensemble clock ; les événements fd sont explicitement ENOSYS. Les horloges sont ramenées à une origine commune entre workers, au prix de la précision du Number sur les nanosecondes.

**Limite de preuve de ce périmètre.** Les horloges CPU non implémentées se comportent comme monotonic et sched_yield est un no-op ; ceci n’est pas une implémentation complète de preview1. Pas de précision temporelle mesurée.

<a id="finding-host-web-ring-signed"></a>

### HOST-WEB-RING-SIGNED — L’anneau stdin utilise des indices négatifs après 2 Gio cumulés

**P2 · défaut · confiance forte.** Périmètres : HOST-009.

**Mécanisme identifié.** head et tail sont lus dans Int32Array. La quantité disponible est corrigée par >>>0, mais les indices utilisent directement (head+k)%capacity et (tail+done+k)%capacity. En JavaScript, le reste d’un entier négatif est négatif. Après le bit31, les cases négatives ne désignent plus les octets du tableau.

**Condition à vérifier.** Une même session transporte au moins2^31 octets cumulés ; le défaut ne nécessite pas un buffer résident de cette taille.

**Impact.** Les écritures peuvent être perdues et les lectures produire des zéros depuis undefined ; le flux LSP est corrompu.

**Direction de correction.** Convertir le compteur en non signé avant le modulo, ou appliquer un masque capacity-1 puisque la capacité est une puissance de deux, des deux côtés.

**Sources à relire.**

- [docs/demo/server.js:118](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/docs/demo/server.js#L118) — Calcul de capacité et index signé en123.
- [docs/demo/wasi-worker.js:126](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/docs/demo/wasi-worker.js#L126) — Différence non signée mais index signé en137.

**Contexte HOST-009 — Anneau partagé pour stdin dans la démo.** Avant le rebouclage signé, le producteur écrit les bytes puis publie head atomiquement ; le lecteur observe head avant de copier et publie tail après la lecture. La différence modulo2^32 fonctionne tant que le nombre d’octets en vol ne dépasse pas la capacité. La boucle de lecture évite le réveil perdu en attendant la valeur head déjà lue. En revanche l’index physique n’applique pas la même arithmétique non signée, défaut confirmé par relecture croisée. Côté producteur, pending n’est pas borné et chaque send sur anneau plein peut planifier son propre timer pump à1ms : l’anneau borne les bytes partagés, pas la file JS ni le nombre de timers.

**Limite de preuve de ce périmètre.** La fréquence et le coût des timers ne sont pas mesurés. Le champ closed existe côté lecteur mais dispose termine directement les workers : ce client ne propose pas une fermeture stdin drainée.

<a id="finding-host-web-session-error"></a>

### HOST-WEB-SESSION-ERROR — Les erreurs asynchrones de démarrage ne suivent pas toutes la session active

**P2 · défaut · confiance forte.** Périmètres : HOST-013.

**Mécanisme identifié.** Les succès de startServer sont filtrés par session.disposed, mais son catch modifie state/error et le statut global sans ce filtre. Un ancien démarrage peut donc marquer stopped la nouvelle session. Un échec de initialize est avalé en null sans disposer le serveur ni passer le statut à stopped ; openLanguage refuse ensuite une nouvelle ouverture du même langage tant que ce serveur reste running.

**Condition à vérifier.** Changement rapide de langage pendant un chargement qui échoue, ou réponse JSON-RPC d’erreur à initialize sans sortie du serveur.

**Impact.** Statut/restart rattachés à la mauvaise session, ou interface bloquée à starting alors qu’un serveur non initialisé reste actif.

**Direction de correction.** Encadrer toutes les étapes de démarrage dans un gestionnaire d’échec attaché à la session ; vérifier son identité avant publication et disposer le serveur en cas d’échec d’initialisation.

**Sources à relire.**

- [docs/demo/app.js:173](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/docs/demo/app.js#L173) — Session créée et garde du même langage.
- [docs/demo/app.js:213](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/docs/demo/app.js#L213) — Catch sans contrôle disposed.
- [docs/demo/app.js:239](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/docs/demo/app.js#L239) — Initialize et catch en259.

**Contexte HOST-013 — Changement et redémarrage du langage de démo.** Les chemins de succès conservent une référence session et contrôlent disposed après fetch et après startServer ; les modèles et l’ancien serveur sont détruits par stopCurrent. Le redémarrage force la destruction avant réouverture et conserve le nom du document sélectionné. Les chemins d’erreur ne suivent pas tous cette discipline. En outre les fetch des exemples sont hors du try, sans response.ok : une page HTTP404 peut devenir le texte DSL, et un rejet depuis le listener de clic n’est pas repris par main().catch.

**Limite de preuve de ce périmètre.** Pas d’automatisation de navigateur ni de mesure de course. La perte volontaire des modifications au redémarrage découle de la destruction des modèles et n’est pas présentée comme persistance promise.

<a id="finding-host-web-worker-lost-wake"></a>

### HOST-WEB-WORKER-LOST-WAKE — Le pool web peut perdre le réveil qui démarre un pthread

**P2 · défaut · confiance forte.** Périmètres : HOST-011.

**Mécanisme identifié.** La boucle d’attente lit le slot dans sa condition puis le relit pour fournir la valeur attendue à Atomics.wait. Si la première lecture voit0/1 et que threadSpawn publie2 et notify entre les deux, la seconde lecture vaut2. Le worker attend alors que2 change, alors que c’est lui seul qui doit faire la transition vers3 après son réveil.

**Condition à vérifier.** Interleaving de publication entre les deux lectures à la ligne316.

**Impact.** Le slot reste bloqué à start et le pthread ne démarre pas ; un join côté C++ peut attendre indéfiniment.

**Direction de correction.** Lire le slot une seule fois par itération, sortir si2, sinon attendre cette même valeur observée ; ne pas recharger la valeur attendue après le test.

**Sources à relire.**

- [docs/demo/wasi-worker.js:274](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/docs/demo/wasi-worker.js#L274) — Réservation, publication2 et notify.
- [docs/demo/wasi-worker.js:314](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/docs/demo/wasi-worker.js#L314) — Double lecture avant wait.

**Contexte HOST-011 — Slots de threads de la démo.** Le protocole de slot suit0→1 par CAS, écrit tid/startArg, puis publie2 et notifie ; ces arguments précèdent donc le signal atomique. Le worker ne réutilise une instance qu’après en avoir créé une nouvelle, et annonce idle après instanciation. Les4 slots fixent la concurrence disponible et le refus -EAGAIN est explicite. Le défaut de wait est indépendant du quota et a été confirmé par une seconde lecture. NEXT_TID n’est pas plafonné au domaine wasi-threads, contrairement au runner Node ; cette limite ne devient pertinente qu’après un nombre extrême de créations.

**Limite de preuve de ce périmètre.** Aucun interleaving exécuté ; le constat donne le chemin possible par les opérations atomiques. Les échecs de startup simultanés peuvent laisser certaines promesses ready pendantes après stop, qui termine les workers sans rejeter chacun des waits de startup.

<a id="finding-host-windows-nonexistent-path"></a>

### HOST-WINDOWS-NONEXISTENT-PATH — Les arguments de chemins Windows non existants ne sont pas traduits

**P2 · écart d’API · confiance forte.** Périmètres : HOST-003.

**Mécanisme identifié.** guestArguments ne convertit un argument que si exists(arg) répond vrai. Un chemin absolu Windows destiné à être créé reste sous la forme C:\... alors que les lecteurs WASI utilisent /c:/.... À l’inverse tout argument existant est converti sans connaître son rôle.

**Condition à vérifier.** CLI WASI recevant un chemin de sortie absent, ou une valeur non-chemin qui coïncide avec un fichier existant.

**Impact.** Le comportement dépend de l’existence du chemin avant lancement ; les consommateurs du runner doivent prétraduire certains arguments.

**Direction de correction.** Définir une politique de conversion basée sur les formes de chemin ou des paramètres explicitement désignés, sans utiliser l’existence comme seule preuve de leur rôle.

**Sources à relire.**

- [tools/wasi-run.mjs:65](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/tools/wasi-run.mjs#L65) — Mappage des lecteurs vers /lettre:/.
- [tools/wasi-run.mjs:135](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/tools/wasi-run.mjs#L135) — Conversion conditionnée par exists.

**Contexte HOST-003 — Correspondance des chemins hôte/invité.** POSIX préouvre / et conserve les arguments ; Windows préouvre chaque lecteur présent et reconstruit PWD depuis process.cwd. Les lecteurs sont abaissés et séparateurs unifiés. Les chemins UNC sont explicitement hors des lecteurs exposés, et guestPath les rabat sur / : ce fallback ne rend pas un partage accessible. La conversion heuristique des arguments introduit en revanche une différence observable pour les destinations à créer.

**Limite de preuve de ce périmètre.** Pas d’exécution Windows. Les contraintes d’accès sont celles des préouvertures et des droits du processus hôte.

<a id="finding-lex-annulation-token"></a>

### LEX-ANNULATION-TOKEN — Plusieurs scans d’un token long ne consultent pas l’annulation

**P2 · limite · confiance forte.** Périmètres : LEX-025.

**Mécanisme identifié.** LexicalMachine consulte ThreadStop à chaque étape, mais les boucles DFA, runs SWAR générés, recherches de relais et scans de suffixes ne consultent aucun poll. Le parser peut attendre la fin du token ou de sa recherche de fermeture. LexReadBudget concerne seulement un sous-chemin de réparation et facture après la lecture.

**Condition à vérifier.** Annulation pendant un long token régulier, une recherche de fermeture ou un scan sans retour au parser.

**Impact.** Latence d’annulation dépendant de la longueur et du coût lexical ; aucune borne courte assurée.

**Direction de correction.** Propager un poll amorti dans les scans et recherches ; ne pas publier de réponse définitive après interruption.

**Sources à relire.**

- [src/engines/cursiv/shared/runtime/LexWalk.hpp:42](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/shared/runtime/LexWalk.hpp#L42) — DFA sans poll.
- [src/engines/cursiv/shared/gen/LexWalkEmitter.hpp:368](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/shared/gen/LexWalkEmitter.hpp#L368) — SWAR sans poll.
- [src/engines/cursiv/shared/runtime/LexSuffixSession.hpp:147](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/shared/runtime/LexSuffixSession.hpp#L147) — Suffixes sans poll.
- [src/engines/cursiv/shared/runtime/LexReading.hpp:248](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/shared/runtime/LexReading.hpp#L248) — Facturation après lecture.
- [src/engines/cursiv/shared/runtime/LexicalMachine.hpp:208](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/shared/runtime/LexicalMachine.hpp#L208) — Poll du reconnaisseur explicite.

**Contexte LEX-025 — Budget de lecture, annulation et statuts incomplets.** Le budget 64 bits évite le débordement de 4*len et sature à zéro. Son épuisement abandonne les tentatives de tokens ouverts dans le regroupement de texte invalide, sans interrompre le scan normal. Les marches échouées consultent poll tous les 16 octets et publient après résolution ; LexicalMachine consulte le stop entre étapes. Les autres scans sont couverts par le constat.

**Limite de preuve de ce périmètre.** Constat statique, sans mesure de délai ni interruption exécutée.

<a id="finding-ll-simulation-budget-differe"></a>

### LL-SIMULATION-BUDGET-DIFFERE — Une fermeture de réparation échappe au budget externe et à l’annulation

**P2 · limite · confiance forte.** Périmètres : LL-041.

**Mécanisme identifié.** LlSearch vérifie et facture les visites autour des appels au simulateur. seed/start/step/filter peuvent achever une fermeture entière avant ce contrôle. Le budget interne sert uniquement à la tentative sérialisée et drain ne poll pas le stop externe.

**Condition à vérifier.** Grande fermeture epsilon ou nombreux contextes avant le prochain token, y compris à l’amorçage et au replay.

**Impact.** Dépassement possible du budget de réparation et retard d’annulation pendant une fermeture ; les seuils de cache entre recherches ne la bornent pas.

**Direction de correction.** Passer une limite de visites restante et un poll au simulateur, puis propager son statut incomplet avant publication.

**Sources à relire.**

- [src/engines/cursiv/ll/runtime/LlSearch.hpp:55](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/ll/runtime/LlSearch.hpp#L55) — simulateur avec limite zéro
- [src/engines/cursiv/ll/runtime/ll_search.hpp:188](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/ll/runtime/ll_search.hpp#L188) — paiement différé
- [src/engines/cursiv/ll/table/LlSimulation.hpp:828](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/ll/table/LlSimulation.hpp#L828) — budget interne de comparaison
- [src/engines/cursiv/ll/table/LlSimulation.hpp:840](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/ll/table/LlSimulation.hpp#L840) — drain sans stop externe

**Contexte LL-041 — Budget externe et propagation de l’annulation.** Les scans lexicaux de réparation reçoivent un horizon artificiel, paient les octets inspectés et rejettent leur résultat si ce bord a pu changer sa signification. Les points externes de recherche pollent l’annulation. En revanche LlSearch construit LlSimulation avec sa limite par défaut zéro ; le simulateur n’a ni callback de stop ni budget externe et ses visites ne sont facturées qu’après retour.

**Limite de preuve de ce périmètre.** À distinguer de LEX-ANNULATION-TOKEN, qui concerne le balayage d’un token ; les deux peuvent s’additionner.

<a id="finding-lr-caret-cache-dense"></a>

### LR-CARET-CACHE-DENSE — Le cache d’échantillons alloue modes × terminaux dès sa première utilisation

**P2 · limite · confiance forte.** Périmètres : LR-067.

**Mécanisme identifié.** cells alloue un tableau d’atomiques de count=modeTight.size()*terms.size(), même si seules quelques paires sont demandées. L’espace utile nominal est sizeof(atomic<uint32_t>)*M*T ; le produit maximal des domaines 16 bits devient très important. Plusieurs premiers appels concurrents peuvent allouer chacun le tableau complet avant le CAS, puis libérer leurs doublons.

**Condition à vérifier.** Parser avec conditions textuelles utilisant les échantillons et beaucoup de modes/terminaux ; premières requêtes éventuellement concurrentes.

**Impact.** Pic d’allocation supérieur au cache final et mémoire retenue pour des paires jamais consultées. Aucune course de publication ni RSS mesurée.

**Direction de correction.** Allouer par mode/bloc à la demande ou utiliser un cache clairsemé borné, et sérialiser l’initialisation coûteuse si nécessaire.

**Sources à relire.**

- [src/engines/cursiv/lr/runtime/LrCaretTable.cpp:2076](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/LrCaretTable.cpp#L2076) — Allocation avant compétition CAS.
- [src/engines/cursiv/lr/runtime/LrCaretTable.cpp:2352](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/LrCaretTable.cpp#L2352) — Produit modes × terminaux.
- [src/engines/cursiv/lr/runtime/LrCaretTable.hpp:37](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/LrCaretTable.hpp#L37) — Rétention jusqu’à destruction.

**Contexte LR-067 — Cache partagé des mots d'échantillonnage.** Le tableau atomique est publié par CAS acquire/release ; le perdant libère son allocation privée et réutilise la gagnante. Chaque cellule est un masque autonome avec bit known, donc les stores relaxed concurrents de la même valeur sont suffisants ; le résultat dépend de mode/terminal et de tables immuables, sans texte de document. La destruction exige que les appels sur le parser aient terminé, contrat usuel d’ownership. L’allocation est cependant dense et reste vivante jusqu’à la destruction du parser.

**Limite de preuve de ce périmètre.** Analyse statique uniquement ; aucune requête de complétion ni grammaire de démonstration exécutée. Les coûts décrits sont structurels, pas mesurés.

<a id="finding-lr-caret-echantillons-incomplets"></a>

### LR-CARET-ECHANTILLONS-INCOMPLETS — Les neuf échantillons fixes ne garantissent pas l’exhaustivité des terminaux conditionnels

**P2 · limite · confiance forte.** Périmètres : LR-066.

**Mécanisme identifié.** Pour un terminal sans spellings, offer({}) produit d’abord des features abstraites. Si une condition a été consultée et qu’au moins un des neuf échantillons est lexicalement accepté, out.resize(mine) supprime ce résultat puis seuls ces échantillons sont essayés. Le fait qu’aucun échantillon ne satisfasse une condition ne prouve pas qu’aucune autre valeur du terminal ne la satisfait.

**Condition à vérifier.** Terminal sans spellings explicites, au moins un échantillon lexical reconnu, et valeurs satisfaisant la condition absentes des neuf chaînes fixes.

**Impact.** Propositions potentiellement absentes alors que d’autres valeurs du terminal sont admissibles ; limite d’exhaustivité conditionnelle, sans reproducer grammatical exécuté.

**Direction de correction.** Conserver une feature abstraite marquée incertaine lorsque les échantillons échouent, ou permettre au langage de fournir des témoins conditionnels et un contrat d’exhaustivité.

**Sources à relire.**

- [src/engines/cursiv/lr/runtime/LrCaretTable.cpp:2467](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/LrCaretTable.cpp#L2467) — Passage des features abstraites aux exemples.
- [src/engines/cursiv/lr/runtime/LrCaretTable.cpp:2473](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/LrCaretTable.cpp#L2473) — Effacement avant essais bornés.
- [src/engines/cursiv/shared/runtime/StampedGrammar.hpp:114](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/shared/runtime/StampedGrammar.hpp#L114) — Neuf échantillons constants : a, name, 0, 1, _, deux chaînes vides et deux fins de ligne.

**Contexte LR-066 — Essai de candidats et projection des features.** Les candidats passent par leurs spellings, l’élection lexicale, les guards/gaps, le maintien de la lecture précédente et les passes de réduction. Une feature est filtrée par sa landing et dédupliquée dans le groupe du terminal ; les différents sites/types ne sont pas écrasés. Les essais restaurent les piles par rollback. Les conditions peuvent être interrogées sur une valeur inconnue puis raffinées par échantillons, avec la limite d’exhaustivité décrite. Les coûts des dédups sont consolidés sous LR-CARET-CACHES-LINEAIRES et l’annulation sous CORE-COMPLETION-BOUNDS.

**Limite de preuve de ce périmètre.** Analyse statique uniquement ; aucune requête de complétion ni grammaire de démonstration exécutée. Les coûts décrits sont structurels, pas mesurés.

Comparaison incluse : [LR-066](#comparison-lr-066).

<a id="finding-lr-caret-hypotheses-combinatoires"></a>

### LR-CARET-HYPOTHESES-COMBINATOIRES — Les conditions différées et les chaînes ordonnées peuvent multiplier les frames de complétion

**P2 · limite · confiance forte.** Périmètres : LR-063.

**Mécanisme identifié.** land_vectors énumère les valuations encore inconnues, puis deferred construit une chaîne d’hypothèses pour chaque landing. pass et assume internent (chaîne antérieure, nouvel élément), sans canoniser un ensemble sémantiquement identique ajouté dans un ordre différent. laid.assumes et laid.passes participent à l’identité de frame ; le partage de piles ne supprime donc pas ces permutations.

**Condition à vérifier.** Grammaires avec conditions qui atteignent le caret et plusieurs chemins ou ordres de réductions.

**Impact.** Croissance combinatoire possible des hypothèses/frames et temps de complétion ; aucune perte sémantique ni explosion mesurée affirmée.

**Direction de correction.** Canoniser les ensembles de contraintes lorsque leur ordre n’a pas de sens, fusionner les valuations équivalentes et offrir un quota avec résultat incomplet explicite.

**Sources à relire.**

- [src/engines/cursiv/lr/runtime/LrCaretTable.hpp:167](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/LrCaretTable.hpp#L167) — Parcours des valuations compatibles.
- [src/engines/cursiv/lr/runtime/LrCaretTable.cpp:1334](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/LrCaretTable.cpp#L1334) — Ajout des hypothèses aux landings.
- [src/engines/cursiv/lr/runtime/LrCaretTable.cpp:1354](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/LrCaretTable.cpp#L1354) — Clé de passe dépendante de la chaîne.
- [src/engines/cursiv/lr/runtime/LrCaretTable.cpp:1390](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/LrCaretTable.cpp#L1390) — Clé de lien dépendante de la chaîne.
- [src/engines/cursiv/lr/runtime/LrCaretTable.cpp:674](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/LrCaretTable.cpp#L674) — Identités inclues dans la frame.

**Contexte LR-063 — Hypothèses de conditions et chaînes de passes.** Les hypothèses mémorisent condition, token, position initiale et valeur ; les contradictions reconnues ne sont pas conservées. Les suffixes de liens pointent vers des entrées plus anciennes, permettant judge en ordre croissant. Les passes distinctes empêchent la réutilisation d’une réduction si le candidat change le token dans un autre mode. Les textes des conditions sont réassemblés au bon offset et étendus tant que la portée lexicale n’est pas connue. Les chaînes sont toutefois identifiées par leur ordre d’ajout, et les valuations inconnues peuvent se multiplier.

**Limite de preuve de ce périmètre.** Analyse statique uniquement ; aucune requête de complétion ni grammaire de démonstration exécutée. Les coûts décrits sont structurels, pas mesurés.

<a id="finding-lr-dp-travail-non-facture"></a>

### LR-DP-TRAVAIL-NON-FACTURE — Les transitions de la borne dynamique ne sont pas toutes facturées au budget de travail

**P2 · limite · confiance forte.** Périmètres : LR-057, LR-058.

**Mécanisme identifié.** run_all appelle step jusqu’à épuisement de oweStack sans poll ni incrément de work. Les hits de cache et transitions outer/inner/drop/typo ne reçoivent pas de charge dédiée. Ahead parcourt les modes et déduplique linéairement ses tokens ; misread fait de même pour ses typos. Les lectures lexicales sont facturées, mais leur réutilisation ne borne pas les transitions/comparaisons de DP.

**Condition à vérifier.** Nombreuses combinaisons de positions/terminaux/modes dans l’horizon de 512 octets, avec caches lexicaux chauds.

**Impact.** Latence de borne disproportionnée au budget work et annulation retardée ; structure finie, mais quota partiel.

**Direction de correction.** Facturer les transitions et comparaisons, poller run_all et rendre la borne triviale si le calcul est coupé.

**Sources à relire.**

- [src/engines/cursiv/lr/runtime/lr_search_bound.hpp:377](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/lr_search_bound.hpp#L377) — Boucle DP sans poll/charge.
- [src/engines/cursiv/lr/runtime/lr_search_bound.hpp:396](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/lr_search_bound.hpp#L396) — Hits sans charge propre.
- [src/engines/cursiv/lr/runtime/lr_search_bound.hpp:455](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/lr_search_bound.hpp#L455) — Transitions par candidat.
- [src/engines/cursiv/lr/runtime/lr_search_bound.hpp:204](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/lr_search_bound.hpp#L204) — Dédup tokens.
- [src/engines/cursiv/lr/runtime/lr_search_bound.hpp:306](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/lr_search_bound.hpp#L306) — Dédup typos.

**Contexte LR-057 — Borne inférieure par programmation dynamique.** La DP est une machine de phases explicite. Elle minimise sur les tokens de tous les modes, leurs suites after, suppressions et relectures, avec horizon et shifts de validation bornés. La borne enrichie est désactivée pour l’indentation/tokens laid. boundShort force le retour à la borne triviale et empêche les mémos incomplets. COM-TYPO-INCOMPLET est une exception : la coupure non signalée peut soustraire un candidat bon marché de la relaxation ; aucun choix final non optimal reproduit. Les références de frames ne sont pas conservées après une entrée qui peut agrandir oweStack. Le coût des transitions est imparfaitement représenté dans work.

**Limite de preuve de ce périmètre.** Analyse statique uniquement ; aucun scénario exécuté ni coût mesuré.

**Contexte LR-058 — Recherche best-first et allocation du travail.** Le tas ordonne la borne/coût puis perte/gestes/fautes/inventions, et ignore les configurations gone. boundAfter déclenche le calcul de la borne sans modifier les coûts ; une borne relevée remet l’état en queue avant expansion. Les buts et furthest distinguent solution validée et repli. Les charges couvrent essais/forks, scans, cactus et gaps, avec les exceptions LR-DP-TRAVAIL-NON-FACTURE et COM-ELECTION-HORS-QUOTA. restart conserve le travail/budget cumulé du document et les capacités de nombreux buffers, tout en réinitialisant la recherche. Aucune optimalité globale au-delà du domaine/quota et des réserves relevées n’est déduite de ce code.

**Limite de preuve de ce périmètre.** Analyse statique uniquement ; aucun scénario exécuté ni coût mesuré.

<a id="finding-lr-group-lines-zero"></a>

### LR-GROUP-LINES-ZERO — L’API directe d’émission accepte une taille de groupe nulle puis divise par zéro

**P2 · écart d’API · confiance forte.** Périmètres : LR-036.

**Mécanisme identifié.** lr_emit_parser_files copie groupLines sans contrôle ; plan_groups calcule ensuite (total + groupLines - 1) / groupLines. La valeur zéro est représentable par la signature publique et provoque une division entière par zéro. Le parseur CLI impose un minimum de 1 et protège son propre chemin.

**Condition à vérifier.** Appel C++ direct de lr_emit_parser_files avec groupLines égal à 0.

**Impact.** Comportement indéfini dans le générateur, typiquement interruption, pour une option invalide qui devrait être refusée proprement.

**Direction de correction.** Valider groupLines à la frontière lr_emit_parser_files et lever un diagnostic explicite, ou documenter et faire respecter un type d’option non nul.

**Sources à relire.**

- [src/engines/cursiv/lr/gen/lr_emit_parser.cpp:2791](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/gen/lr_emit_parser.cpp#L2791) — API et copie non validée.
- [src/engines/cursiv/lr/gen/lr_emit_parser.cpp:2461](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/gen/lr_emit_parser.cpp#L2461) — Division par groupLines.
- [src/engines/cursiv/lr/gen/lr_main.cpp:89](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/gen/lr_main.cpp#L89) — Le chemin CLI impose least=1.

**Contexte LR-036 — Fragments, groupes et unités de compilation.** Les map de string_view pointent vers des vecteurs préalloués dont les textes antérieurs ne sont plus réécrits. enter collecte les entrées intergroupes avant les signatures, et kEnter encode groupe/local sur deux champs de 16 bits dans les domaines bornés du générateur. Les appels intergroupes imposent CURSIV_MUSTTAIL ; on ne peut donc pas déduire une croissance de pile par appels ordinaires sur le chemin supporté. Le namespace et les inline constexpr partagés assurent le raccord des unités. Deux problèmes localisés subsistent : option groupLines=0 non validée et rescans du front de partitionnement.

**Limite de preuve de ce périmètre.** Analyse statique du snapshot ; aucun programme généré compilé ni exécuté, aucune mesure de temps ou de mémoire.

<a id="finding-lr-gss-accept-chemins"></a>

### LR-GSS-ACCEPT-CHEMINS — L’acceptation déroule tous les chemins de pile partagés avant de choisir une dérivation

**P2 · limite · confiance forte.** Périmètres : LR-047, LR-050.

**Mécanisme identifié.** accept appelle each_path avec un callback qui retourne toujours true et construit une copie inversée des forests pour chaque chemin. Une chaîne de embranchements/reconvergences peut représenter bien plus de chemins que de sommets/arêtes. close répète l’opération pour chaque accept, puis elected/steps matérialise le parcours élu avant la boucle du caller qui poll l’annulation.

**Condition à vérifier.** GSS conservant plusieurs embranchements de pile à l’acceptation ; le pruning n’a pas tout dominé.

**Impact.** Temps et mémoire proportionnels aux chemins déroulés et à leurs longueurs, potentiellement exponentiels en embranchements indépendants ; annulation retardée pendant cette phase.

**Direction de correction.** Élire directement sur le graphe partagé ou construire une racine factorisée ; transmettre poll/quota à accept, elect et each_step avant toute matérialisation complète.

**Sources à relire.**

- [src/engines/cursiv/lr/runtime/LrGssGraph.cpp:792](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/LrGssGraph.cpp#L792) — Énumération explicite de chaque chemin.
- [src/engines/cursiv/lr/runtime/LrGssGraph.cpp:827](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/LrGssGraph.cpp#L827) — Copie par chemin sans arrêt.
- [src/engines/cursiv/lr/runtime/LrGssGraph.cpp:785](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/LrGssGraph.cpp#L785) — Acceptation de chaque sommet final.
- [src/engines/cursiv/lr/runtime/LrGssForest.hpp:176](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/LrGssForest.hpp#L176) — Vectorisation des steps.
- [src/engines/cursiv/lr/runtime/LrGss.hpp:154](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/LrGss.hpp#L154) — Premier poll seulement après retour de elected.

**Contexte LR-047 — Construction et élection de la forêt.** L’élection de forêt est itérative avec marques fresh/visiting/elected/ranked ; elle refuse un cycle ou un nœud sans alternative. Les enfants sont élus avant le classement du parent. Les préfixes égaux, le rang lexical initial et les mots de choix sont comparés séparément ; ordered_frame retourne open pour un préfixe strict au lieu de forcer un ordre. Les tableaux choices/choiceAt appendent toujours les mêmes cardinalités et gardent leurs offsets synchronisés. Le mode flat est abandonné globalement et ses rangs invalidés avant passage aux ropes. Les matérialisations each_step/steps déroulent le contenu élu, coût en occurrences et non seulement en nœuds partagés ; leur absence de contrôle avant retour est incluse dans LR-GSS-ACCEPT-CHEMINS.

**Limite de preuve de ce périmètre.** Analyse statique ; aucun scénario exécuté, mesure de latence/mémoire ou atteinte des limites extrêmes affirmée.

**Contexte LR-050 — Acceptation et retour à une pile linéaire.** Le retour linéaire exige une seule branche fraîche, l’absence d’acceptation concurrente, une pile linéaire et un nombre suffisant de shifts après la dernière mort. Le point held est repris seulement après validation de progression. L’acceptation conserve les chemins dans une forêt de choix ; le replay vérifie ensuite état et offset à chaque étape. Toutefois, la conservation est obtenue en déroulant tous les chemins de pile avant élection et peut annuler le bénéfice du partage.

**Limite de preuve de ce périmètre.** Analyse statique ; aucun scénario exécuté, mesure de latence/mémoire ou atteinte des limites extrêmes affirmée.

<a id="finding-lr-gss-election-shifts-quadratique"></a>

### LR-GSS-ELECTION-SHIFTS-QUADRATIQUE — L’élection lexicale du GSS compare chaque shift à tous les autres

**P2 · limite · confiance forte.** Périmètres : LR-052.

**Mécanisme identifié.** Hors cas où tous les tokens sont identiques, elected_stand calcule beaten par un any_of sur la totalité de _shifts pour chaque Shift. Les poids sont mémorisés, mais les comparaisons de bornes/symboles restent quadratiques au pire ; des appels successifs peuvent réexaminer les shifts encore au front. Le poll de l’épisode se situe avant l’appel.

**Condition à vérifier.** Beaucoup de shifts simultanés avec tokens distincts, longueurs ou modes rivaux.

**Impact.** Latence O(S²) dans une étape et annulation retardée jusqu’au retour ; exactitude de l’élection non mise en cause.

**Direction de correction.** Regrouper par début de token, déterminer longueur/poids dominants une fois par groupe et poller les gros lots.

**Sources à relire.**

- [src/engines/cursiv/lr/runtime/LrGss.hpp:402](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/LrGss.hpp#L402) — Cas rapide puis élection.
- [src/engines/cursiv/lr/runtime/LrGss.hpp:415](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/LrGss.hpp#L415) — Boucle de toutes les paires de shifts.
- [src/engines/cursiv/lr/runtime/LrGss.hpp:130](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/LrGss.hpp#L130) — Poll externe à cette phase.

**Contexte LR-052 — Boucle d'épisode et replay des actions.** La fenêtre initiale de32 frames est multipliée par4 jusqu’à la profondeur réelle uniquement sur underflow. Les copies de frames sur cette suite géométrique restent O(profondeur), mais l’épisode est rejoué à chaque extension. Les polls entourent les itérations principales ; certaines opérations internes regroupent beaucoup de travail. open copie sa Reading avant les allocations, garde ses candidats dans une room locale, et finish reconstruit le premier état requis pour les guards. replay vérifie état/offset avant l’écriture, réinstalle mode/virtuel/indentation et relit les trivia avec exclusions si nécessaire.

**Limite de preuve de ce périmètre.** Analyse statique ; aucun scénario exécuté, mesure de latence/mémoire ou atteinte des limites extrêmes affirmée.

<a id="finding-lr-gss-read-past-global"></a>

### LR-GSS-READ-PAST-GLOBAL — Le calcul des progrès de réparation rescane toutes les arêtes jusqu’au fixpoint

**P2 · limite · confiance forte.** Périmètres : LR-051.

**Mécanisme identifié.** read_past alloue un petit tableau de bornes par sommet, puis parcourt tous les arcs à chaque itération tant qu’un changement a lieu. Chaque case croît monotonement, donc le fixpoint converge, mais l’ordre d’insertion des arcs peut imposer plusieurs passes globales. Les comparaisons de sources parcourent en plus les suffixes de forêts et forks. Aucun poll ou quota ne borne ces scans dans sources.

**Condition à vérifier.** Grand épisode GSS avec propagation tardive ou ordre d’arcs défavorable.

**Impact.** Coût O(nombre de passes*arcs*nombre de shifts de validation), auquel s’ajoutent les preuves d’ordre des sources ; latence avant admission au réparateur.

**Direction de correction.** Propager seulement les sommets modifiés via worklist et facturer/poller les comparaisons de sources.

**Sources à relire.**

- [src/engines/cursiv/lr/runtime/LrGssGraph.cpp:841](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/LrGssGraph.cpp#L841) — Fixpoint sur tous les arcs.
- [src/engines/cursiv/lr/runtime/LrGssGraph.cpp:911](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/LrGssGraph.cpp#L911) — Comparaison exacte des suffixes de sources.
- [src/engines/cursiv/lr/runtime/LrGss.hpp:183](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/LrGss.hpp#L183) — Préparation hors boucle de recherche.

**Contexte LR-051 — Sélection des sources de réparation.** Les sources sont filtrées par les progrès validés, puis classées par forêt et divergences de forks. L’énumération <=cap est exhaustive par frozen ; au-delà, le générateur best-first remplace ce lot et note une coupure, donc il ne prétend pas avoir conservé toutes les piles. Le classement final merge_sort évite d’exiger arbitrairement l’ordre strict d’un tri standard sur des préférences contextualisées. frames_of partage conceptuellement la provenance mais recopie une pile par source pour la recherche ; les sources vivent jusqu’à la fin de celle-ci.

**Limite de preuve de ce périmètre.** Analyse statique ; aucun scénario exécuté, mesure de latence/mémoire ou atteinte des limites extrêmes affirmée.

<a id="finding-lr-gss-sources-cap-local"></a>

### LR-GSS-SOURCES-CAP-LOCAL — Le plafond de 512 sources de réparation s’applique à chaque arrêt gelé, pas au lot global

**P2 · limite · confiance forte.** Périmètres : LR-051.

**Mécanisme identifié.** sources(cap) réinitialise begin/taken/pending pour chaque frozen et accumule les résultats dans found, trié globalement sans troncature. L’appelant passe512, peut donc recevoir F*512 sources pour F arrêts admissibles. L’énumération initiale produit même cap+1 chemins avant de basculer. Les dérivations, classements et copies de piles sont préparés avant le budget de recherche.

**Condition à vérifier.** Nombreux arrêts gelés d’un même épisode GSS, chacun avec plusieurs piles.

**Impact.** Allocation et temps préparatoire non bornés par512 globalement ; multiplication du nombre de piles recopiées.

**Direction de correction.** Ajouter un plafond/admission global explicite, facturer la préparation et préserver la provenance des coupures dans les diagnostics.

**Sources à relire.**

- [src/engines/cursiv/lr/runtime/LrGss.hpp:41](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/LrGss.hpp#L41) — Constante512.
- [src/engines/cursiv/lr/runtime/LrGss.hpp:183](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/LrGss.hpp#L183) — Collecte complète avant recherche.
- [src/engines/cursiv/lr/runtime/LrGssGraph.cpp:970](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/LrGssGraph.cpp#L970) — Compteur relatif au frozen courant.
- [src/engines/cursiv/lr/runtime/LrGssGraph.cpp:1015](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/LrGssGraph.cpp#L1015) — Tri global sans troncature.

**Contexte LR-051 — Sélection des sources de réparation.** Les sources sont filtrées par les progrès validés, puis classées par forêt et divergences de forks. L’énumération <=cap est exhaustive par frozen ; au-delà, le générateur best-first remplace ce lot et note une coupure, donc il ne prétend pas avoir conservé toutes les piles. Le classement final merge_sort évite d’exiger arbitrairement l’ordre strict d’un tri standard sur des préférences contextualisées. frames_of partage conceptuellement la provenance mais recopie une pile par source pour la recherche ; les sources vivent jusqu’à la fin de celle-ci.

**Limite de preuve de ce périmètre.** Analyse statique ; aucun scénario exécuté, mesure de latence/mémoire ou atteinte des limites extrêmes affirmée.

<a id="finding-lr-lowering-profondeur"></a>

### LR-LOWERING-PROFONDEUR — Des parcours du lowering restent récursifs selon la profondeur de grammaire

**P2 · risque · confiance forte.** Périmètres : LR-003.

**Mécanisme identifié.** ended, ambient, capture_created, head_turns, holds_turn, copy et lr_emit_alts empilent des appels natifs suivant les expressions ; la distribution des directives recopie aussi les vecteurs d'actions des ancêtres. Les parcours itératifs de séquences simples ne couvrent pas ces branches.

**Condition à vérifier.** Une GrammarImage contenant une forte imbrication de scopes, répétitions, alternatives ou directives, admise par les étapes précédentes.

**Impact.** Consommation de pile proportionnelle à la profondeur et volume de copies pouvant croître comme la somme des longueurs des préfixes ; aucun débordement effectif observé.

**Direction de correction.** Utiliser une pile explicite pour les parcours concernés et des chaînes d'actions persistantes, ou imposer un plafond de profondeur documenté à l'entrée.

**Sources à relire.**

- [src/engines/cursiv/lr/gen/LrLowerer.cpp:47](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/gen/LrLowerer.cpp#L47) — ended récursif mémoïsé
- [src/engines/cursiv/lr/gen/LrLowerLay.cpp:58](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/gen/LrLowerLay.cpp#L58) — ambient propage récursivement les bords
- [src/engines/cursiv/lr/gen/LrLowerPool.hpp:147](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/gen/LrLowerPool.hpp#L147) — head_turns/holds_turn/copy
- [src/engines/cursiv/lr/gen/lr_lower_epsilon.hpp:372](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/gen/lr_lower_epsilon.hpp#L372) — lr_emit_alts récursif avec lr_cat

**Contexte LR-003 — Application des directives aux formes intermédiaires.** Les nodes sont identifiés par indices stables, et copy lit une valeur locale avant les push pouvant réallouer. Les copies profondes reprennent aussi les tables unorderedHead/Tail. ambient ne pose les bords que sur les enfants porteurs de position ; capture_created isole le receiver des scopes qui créent ou nestent. Les directives et leurs actions sont transmises dans l'ordre innermost-first. Ces garanties sémantiques ne suppriment pas la récursion native des parcours cités.

**Limite de preuve de ce périmètre.** Analyse statique des sources et des raccords cités ; aucune exécution de grammaire ni mesure de ressources.

<a id="finding-lr-optional-gate-interne"></a>

### LR-OPTIONAL-GATE-INTERNE — optionalGate n'est validé que par la CLI

**P2 · écart d’API · confiance forte.** Périmètres : GRA-006, LR-004.

**Mécanisme identifié.** lr_lower copie LowerDesc.optionalGate sans validation. lr_emit_subsets ne transforme en auxiliaires que les options au-delà de cette valeur, puis évalue 1u << nopt dans la condition de boucle avant le lowerFail de son corps. Sur les plateformes usuelles à unsigned 32 bits, optionalGate >=32 et au moins 32 options laissent un décalage de 32 ou plus, au comportement indéfini. Entre 17 et 31, prioritySubset 16 bits perd aussi une partie du rang.

**Condition à vérifier.** Appel direct de lr_lower avec un LowerDesc dont optionalGate dépasse la borne CLI 15 et une séquence assez optionnelle ; la CLI standard refuse cette configuration.

**Impact.** Entrée interne non totale : décalage invalide ou ordre greedy tronqué au lieu d'un refus explicite.

**Direction de correction.** Valider optionalGate dans lr_lower avec la même borne 0..15 que la CLI avant tout lowering ; ne pas compter sur un refus de capacité ultérieur.

**Sources à relire.**

- [src/engines/cursiv/lr/gen/LrLowerer.cpp:77](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/gen/LrLowerer.cpp#L77) — copie sans garde
- [src/engines/cursiv/lr/gen/lr_main.cpp:93](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/gen/lr_main.cpp#L93) — CLI limite à 15
- [src/engines/cursiv/lr/gen/lr_lower_epsilon.hpp:84](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/gen/lr_lower_epsilon.hpp#L84) — ne réduit nopt qu'au gate demandé
- [src/engines/cursiv/lr/gen/lr_lower_epsilon.hpp:99](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/gen/lr_lower_epsilon.hpp#L99) — shift avant lowerFail
- [src/engines/cursiv/lr/gen/lr_lower_epsilon.hpp:164](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/gen/lr_lower_epsilon.hpp#L164) — rang converti en uint16

**Contexte GRA-006 — Descriptions et paramètres communs du lowering.** LowerDesc sépare terminaux de parsing/prédicat, non-terminaux, actions, règles d’entrée et paramètres d’indentation. LowerDescOf fixe l’ordre des IDs à partir des listes de types et garde une ligne par action publiée. LowerVerdicts distingue limites d’encodage, incohérences lexicales et formes non prises en charge, avec messages qui proposent une reformulation. optionalGate est cependant un uint32 public sans invariant de construction et son consommateur LR n’applique pas les mêmes bornes que la CLI.

**Limite de preuve de ce périmètre.** Les pointeurs et spans du descripteur supposent des données cohérentes ; l’API interne ne valide pas un descripteur arbitrairement forgé. Le défaut optionalGate est partagé avec LR-003.

**Contexte LR-004 — Répétitions et positions optionnelles.** Les répétitions finies utilisent une chaîne de max-min auxiliaires avec branche vide par tour ; l'infini est abaissé en récursion gauche, avec dernier élément séparé lors d'une fermeture de région. Les options excédant le gate deviennent des auxiliaires dont les actions n'appartiennent qu'à la branche présente. EOF interdit un terminal ultérieur et peut être matérialisé avant un non-terminal nullable. Sous la précondition CLI gate<=15, les sous-ensembles coûtent O(n*2^gate), avec refus 16 bits des productions ; l'API directe ne garantit pas cette précondition.

**Limite de preuve de ce périmètre.** Analyse statique des sources et des raccords cités ; aucune exécution de grammaire ni mesure de ressources.

<a id="finding-lr-pools-croissance-16"></a>

### LR-POOLS-CROISSANCE-16 — Les pools de graphe réservent jusqu'à presque seize fois leur taille

**P2 · limite · confiance forte.** Périmètres : LR-011.

**Mécanisme identifié.** lr_room multiplie la capacité par 16 jusqu'à couvrir want, et lr_append l'applique aux kernels, closures, lookaheads et arêtes. À un franchissement de capacité C, la réserve suivante est 16C : l'ancienne et la nouvelle allocation peuvent coexister lors du déplacement.

**Condition à vérifier.** Génération d'une grammaire dont un pool franchit une capacité après le poolHint initial.

**Impact.** Capacité réservée proche de 16 fois les éléments utilisés et besoin transitoire proche de 17C éléments pour un franchissement C→16C ; valeur analytique, pas une mesure RSS.

**Direction de correction.** Employer une croissance plus modérée et suivre la somme des capacités des pools dans les métriques de génération.

**Sources à relire.**

- [src/engines/cursiv/lr/gen/lr_build.cpp:153](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/gen/lr_build.cpp#L153) — croissance x16
- [src/engines/cursiv/lr/gen/lr_build.cpp:521](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/gen/lr_build.cpp#L521) — réserves initiales
- [src/engines/cursiv/lr/gen/lr_build.cpp:584](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/gen/lr_build.cpp#L584) — kernels
- [src/engines/cursiv/lr/gen/lr_build.cpp:764](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/gen/lr_build.cpp#L764) — closures
- [src/engines/cursiv/lr/gen/lr_build.cpp:369](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/gen/lr_build.cpp#L369) — arêtes

**Contexte LR-011 — Kernels et graphe d'états LR.** Le domaine d'items range les productions par dot ; harvest énumère les bits dans cet ordre et laisse le scratch effacé. Les kernels sont comparés exactement après hash, avec tags known/truth dans les deux modes et lookaheads seulement dans WithLa. Les copies dans harv évitent de garder une référence de kernel pendant find_or_add ; closPool ne croît qu'entre visites d'états. L'identifiant d'état est refusé avant passage en uint16. Le coût mémoire est aggravé par la politique de capacité revalidée ci-dessous.

**Limite de preuve de ce périmètre.** Revue statique des fonctions, de leurs structures et des appels cités ; pas d'exécution ni de mesure.

<a id="finding-lr-runtime-ids32-sans-garde"></a>

### LR-RUNTIME-IDS32-SANS-GARDE — Plusieurs arènes runtime tronquent leurs IDs à 32 bits sans refus en release

**P2 · risque · confiance forte.** Périmètres : LR-045, LR-046, LR-047, LR-053, LR-062.

**Mécanisme identifié.** Les pools grandissent avec size_t, puis leurs positions sont converties en uint32_t pour les sommets/arêtes, nœuds de forêt et ropes. Runs ne vérifie at+n<=0xFFFFFFFF que par assert ; CaretStacks réduit aussi ses IDs size_t dans l’index et dans une clé 32+32 bits. Atteindre la sentinelle ou dépasser cette largeur peut donc produire collisions d’identité et indices invalides au lieu d’un refus contrôlé. LrStack a, lui, une garde explicite.

**Condition à vérifier.** Épisode exceptionnel produisant environ 2^32 objets/positions physiques, sous réserve d’une mémoire suffisante ; le nombre d’objets peut dépasser le nombre d’octets du document.

**Impact.** Risque de corruption des identités ou d’accès invalide à une échelle extrême ; aucune atteinte pratique ni exploitation démontrée.

**Direction de correction.** Centraliser les conversions d’IDs avec un contrôle release avant la sentinelle et refuser l’allocation par length_error ou diagnostic borné.

**Sources à relire.**

- [src/engines/cursiv/lr/runtime/LrGssRuns.hpp:63](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/LrGssRuns.hpp#L63) — Garde uniquement assert.
- [src/engines/cursiv/lr/runtime/LrGssGraph.cpp:152](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/LrGssGraph.cpp#L152) — Conversion de sommets.
- [src/engines/cursiv/lr/runtime/LrGssGraph.cpp:202](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/LrGssGraph.cpp#L202) — Conversion d’arêtes.
- [src/engines/cursiv/lr/runtime/LrGssForest.hpp:47](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/LrGssForest.hpp#L47) — Conversion de nœuds.
- [src/engines/cursiv/lr/runtime/LrGssRope.hpp:344](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/LrGssRope.hpp#L344) — Conversion de rope.
- [src/engines/cursiv/lr/runtime/LrCaretTable.cpp:678](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/LrCaretTable.cpp#L678) — Réduction Id size_t vers index32.
- [src/engines/cursiv/lr/runtime/LrCaretTable.cpp:721](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/LrCaretTable.cpp#L721) — Clé parent/enfant sur deux champs32.
- [src/engines/cursiv/lr/runtime/LrCactus.hpp:106](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/LrCactus.hpp#L106) — Le cactus convertit sa taille en ID32 sans garde release.

**Contexte LR-045 — Arènes et index du GSS.** Les pools par chunks conservent les adresses des éléments malgré croissance de la liste des chunks. Runs aligne un run qui ne tient pas dans le chunk courant, et alloue ses chunks contigus ensemble ; size compte les éléments ajoutés et non le padding physique. clear libère les chunks mais les index peuvent garder leurs capacités. L’index confronte chaque collision au prédicat exact et réindexe à charge limitée ; l’élimination d’entrées anciennes est paresseuse. Les limites d’IDs runtime ne sont cependant pas toutes validées en release.

**Limite de preuve de ce périmètre.** Analyse statique ; aucun scénario exécuté, mesure de latence/mémoire ou atteinte des limites extrêmes affirmée.

**Contexte LR-046 — Séquences persistantes et résumés de forks.** La rope est persistante et AVL : concat copie les descripteurs avant récursion, join conserve les agrégats et gss_length_add refuse le dépassement uint64. Sa hauteur est logarithmique en longueur logique et les piles de curseur de 128 niveaux suivent cette borne. common descend quand les formes diffèrent et compare les feuilles exactes ; l’identité d’un sous-arbre sert seulement de raccourci sûr. concat_all réserve le choix quadratique aux <=8 fragments, puis divise en moitiés. Digest attend l’élection immuable des enfants avant de mémoriser leurs forks. Le risque d’IDs physiques32 est consolidé dans LR-RUNTIME-IDS32-SANS-GARDE.

**Limite de preuve de ce périmètre.** Analyse statique ; aucun scénario exécuté, mesure de latence/mémoire ou atteinte des limites extrêmes affirmée.

**Contexte LR-047 — Construction et élection de la forêt.** L’élection de forêt est itérative avec marques fresh/visiting/elected/ranked ; elle refuse un cycle ou un nœud sans alternative. Les enfants sont élus avant le classement du parent. Les préfixes égaux, le rang lexical initial et les mots de choix sont comparés séparément ; ordered_frame retourne open pour un préfixe strict au lieu de forcer un ordre. Les tableaux choices/choiceAt appendent toujours les mêmes cardinalités et gardent leurs offsets synchronisés. Le mode flat est abandonné globalement et ses rangs invalidés avant passage aux ropes. Les matérialisations each_step/steps déroulent le contenu élu, coût en occurrences et non seulement en nœuds partagés ; leur absence de contrôle avant retour est incluse dans LR-GSS-ACCEPT-CHEMINS.

**Limite de preuve de ce périmètre.** Analyse statique ; aucun scénario exécuté, mesure de latence/mémoire ou atteinte des limites extrêmes affirmée.

**Contexte LR-053 — Piles cactus et retour arrière d'une recherche.** Le cactus partage les états/parents, tandis que les begins transportent séparément les origines gelées et les compteurs de chaque lecture. rewind détache les nœuds en sens inverse avant de tronquer les begins. Le move-to-front conserve les listes de frères. Leurs scans incrémentent _stepped puis sont facturés par pay(kRepairLexBytes) : le coût est différé, pas entièrement ignoré. Le climb conserve le début de la première frame réduite et traite epsilon depuis le compteur courant. Les conversions d’IDs relèvent de LR-RUNTIME-IDS32-SANS-GARDE.

**Limite de preuve de ce périmètre.** Analyse statique uniquement ; aucun scénario exécuté ni coût mesuré.

**Contexte LR-062 — Frames et fermeture des piles au caret.** La clé de frame inclut état, offset, lookahead/mode, indentation, implicites, lecture et identités d’hypothèses/passes ; une collision de hash est vérifiée par égalité exacte. Les parents multiples sont dédupliqués, et l’ajout tardif d’un parent déclenche grown : relecture du nœud terminé et propagation aux dépendants de fermeture. below parcourt les niveaux de parents en triant/dédupliquant chaque front. mark/rollback restaure les tailles des frames/blocs/pool après un essai ; les worklists de fermeture sont réinitialisées avant le suivant. La saturation reste sans quota propre, couverte par CORE-COMPLETION-BOUNDS ; les casts 32 bits des IDs sont examinés avec les arènes runtime dans LR045.

**Limite de preuve de ce périmètre.** Analyse statique uniquement ; aucune requête de complétion ni grammaire de démonstration exécutée. Les coûts décrits sont structurels, pas mesurés.

<a id="finding-lr-speculation-quotas-partiels"></a>

### LR-SPECULATION-QUOTAS-PARTIELS — Les quotas de spéculation ne bornent pas son front initial ni toutes ses comparaisons

**P2 · limite · confiance forte.** Périmètres : LR-043, LR-044.

**Mécanisme identifié.** lr_seed_arms crée toutes les alternatives initiales ; LrCellPool prend cap=max(count,64). Le front peut donc dépasser 64. lr_settle_arm effectue les comparaisons par paires avant les checks des quotas, et les suffixes exacts de pile coûtent leur longueur. Les bitsets d’origine occupent slots*ceil(maxArms/64) mots. Le quota trail compte certaines étapes/valeurs, mais pas toute l’expansion d’une longue production, les recherches lexicales préalables ou les preuves exactes en cache.

**Condition à vérifier.** Cellule avec beaucoup d’alternatives initiales et/ou longues piles/productions de même futur.

**Impact.** Latence et allocation initiales potentiellement importantes avant fallback GSS ; les constantes 64/4096/512 ne constituent pas un budget global de temps ou mémoire.

**Direction de correction.** Plafonner l’admission initiale ou basculer directement au GSS, facturer comparaisons et expansion de preuves, et poller l’annulation dans les boucles longues.

**Sources à relire.**

- [src/engines/cursiv/lr/runtime/lr_lookahead.hpp:246](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/lr_lookahead.hpp#L246) — Cap relevé au nombre initial.
- [src/engines/cursiv/lr/runtime/lr_lookahead.hpp:273](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/lr_lookahead.hpp#L273) — Paires avant contrôle.
- [src/engines/cursiv/lr/runtime/lr_lookahead.hpp:369](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/lr_lookahead.hpp#L369) — Admission de toutes les armes.
- [src/engines/cursiv/lr/runtime/lr_lookahead.hpp:62](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/lr_lookahead.hpp#L62) — Produit des dimensions des origines.
- [src/engines/cursiv/lr/runtime/lr_trail.hpp:180](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/lr_trail.hpp#L180) — Expansion par longueur de production.
- [src/engines/cursiv/lr/runtime/lr_trail.hpp:427](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/lr_trail.hpp#L427) — Preuve exacte avant lr_trail_spent.

**Contexte LR-043 — Résolution spéculative d'une cellule.** Les fronts spéculatifs fusionnent après comparaison des positions, lookaheads, indentation/virtuel et classes de pile ; les origines sont unies seulement après preuve de compatibilité des trails. Une coupure crop mène à unsettled et au GSS, pas à une acceptation présentée comme certaine. Le mémo unsettled ne réutilise pas un choix : il évite une nouvelle tentative bornée et déclenche le graphe général. Les budgets comptent des lexes et steps, mais plusieurs coûts préparatoires et comparatifs se trouvent avant les checks.

**Limite de preuve de ce périmètre.** Analyse statique ; aucun scénario exécuté, mesure de latence/mémoire ou atteinte des limites extrêmes affirmée.

**Contexte LR-044 — Traces d'élection et preuves exactes.** Le hash des paths sert d’index, mais un hit ne réutilise un verdict que si exactSize est non nul et que les étapes prod/sym/state/word et le suffixe de pile sont identiques. Les rangs lexicaux dépendant du texte sont recalculés avant la réutilisation du verdict structurel. Les réponses distinguent alike, gap/unjoined, ordre et indécision ; les fusions interarmes indécidées déclenchent le parcours général. Les chemins mémorisés et leur stockage sont bornés séparément, et les collisions remplacent ou recalculent la preuve. Les parcours de preuve hors facturation globale sont couverts par LR-SPECULATION-QUOTAS-PARTIELS.

**Limite de preuve de ce périmètre.** Analyse statique ; aucun scénario exécuté, mesure de latence/mémoire ou atteinte des limites extrêmes affirmée.

<a id="finding-lr-written-cache-partiel"></a>

### LR-WRITTEN-CACHE-PARTIEL — Le seuil trim ne borne pas la mémoire totale de l'ordre écrit

**P2 · limite · confiance forte.** Périmètres : LR-016.

**Mécanisme identifié.** _held compte des unités de chaînes/états/parents, pas des octets. trim ne réinitialise que certains layouts et les parents de Mid ; il garde data, les shapes, rises, origins, scores, back, séquences et résultats de rencontres. Les paires invalidées par une génération ne sont nettoyées qu'à leur prochain accès.

**Condition à vérifier.** Automate avec beaucoup de cellules conflictuelles, couples production/terminal et contextes de retour distincts.

**Impact.** Les seuils 2^26 et 2^22 ne forment pas un plafond mémoire : U couples de scores allouent U×I octets et B demandes back peuvent retenir B×S identifiants, hors layouts ; des mémos périmés peuvent rester alloués jusqu'à destruction de l'analyse.

**Direction de correction.** Comptabiliser les octets/capacités de toutes les familles, retirer les paires invalidées et utiliser des caches évictables pour scores/back lorsque la mémoire dépasse un budget global.

**Sources à relire.**

- [src/engines/cursiv/lr/gen/lr_build.cpp:1894](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/gen/lr_build.cpp#L1894) — trim partiel
- [src/engines/cursiv/lr/gen/lr_build.cpp:1957](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/gen/lr_build.cpp#L1957) — seuils en unités
- [src/engines/cursiv/lr/gen/lr_build.cpp:2227](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/gen/lr_build.cpp#L2227) — back retenu
- [src/engines/cursiv/lr/gen/lr_build.cpp:2275](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/gen/lr_build.cpp#L2275) — une ligne dense du domaine par couple
- [src/engines/cursiv/lr/gen/lr_build.cpp:2961](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/gen/lr_build.cpp#L2961) — nettoyage des paires paresseux
- [src/engines/cursiv/lr/gen/lr_build.cpp:3660](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/gen/lr_build.cpp#L3660) — familles non évincées

**Contexte LR-016 — Formes persistantes de l'ordre écrit.** Les séquences/skeletons sont internés par clés exactes, les identifiants des roots sont bornés à 16 bits et leurs contextes comprennent origins, dot et classe d'admission. Les hashes des rises et origins sont suivis d'égalité complète. Les pointeurs origins ciblent les valeurs de la unordered_map back dont le rehash ne déplace pas les références. Une reconstruction de layout incrémente gen et pair_of invalide ses rencontres à la prochaine demande. Le contrôle de mémoire reste partiel.

**Limite de preuve de ce périmètre.** Revue statique des fonctions, de leurs structures et des appels cités ; pas d'exécution ni de mesure.

<a id="finding-lsp-format-hidden-espaces"></a>

### LSP-FORMAT-HIDDEN-ESPACES — prepend sur un commentaire ignore les demandes d’espaces et de nouvelles lignes

**P2 · limite · confiance forte.** Périmètres : LSP-037.

**Mécanisme identifié.** HiddenNodeFormatter et les régions hidden exposent le même prepend(FormattingAction). Le moteur traite cependant les hidden exclusivement par emit_hidden_edits ; il extrait tabs de l’action pour la marge, sans appliquer characters ou lines au gap. Un commentaire précédé d’un token sur la même ligne conserve sa position.

**Condition à vérifier.** Sur a /*c*/ b, le callback du commentaire appelle prepend(newLine), sans remplacement de contenu. Le commentaire reste après a sur la même ligne. Même omission pour prepend(oneSpace) ou un append du token précédent dont la cible est hidden.

**Impact.** La règle est acceptée mais ne produit pas l’espacement demandé ; le placement des commentaires n’est pas contrôlable par ces combinateurs ordinaires.

**Direction de correction.** Distinguer explicitement les actions de marge des actions de gap, ou exécuter les demandes characters/lines avant la réindentation du commentaire en préservant la fin des commentaires de ligne.

**Sources à relire.**

- [src/lsp/cursiv/formatting/FormattingAction.cpp:73](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/formatting/FormattingAction.cpp#L73) — API transmet toute action prepend.
- [src/lsp/cursiv/formatting/FormattingEngine.cpp:620](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/formatting/FormattingEngine.cpp#L620) — Branche hidden lit le delta tabs seulement.
- [src/lsp/cursiv/formatting/FormattingEngine.cpp:639](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/formatting/FormattingEngine.cpp#L639) — Réindentation seule.
- [src/lsp/cursiv/formatting/FormattingEngine.cpp:374](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/formatting/FormattingEngine.cpp#L374) — Commentaire inline sans remplacement laissé tel quel.
- [src/lsp/cursiv/formatting/FormattingEngine.cpp:645](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/formatting/FormattingEngine.cpp#L645) — Émission ordinaire des gaps exclut hidden.

**Contexte LSP-037 — Nœuds masqués et commentaires.** Les tokens hidden sont trouvés par leur règle de terminal et les callbacks par une clé string propriétaire. replace ne stocke rien si le texte ne change pas ; plusieurs remplacements gardent le dernier. Les callbacks peuvent agrandir le pool d’actions : le moteur relit les pointeurs ensuite et le pending append conserve une clé, évitant des pointeurs de vector invalidés. La réécriture du commentaire est séparée des marges et incorpore la marge dans le remplacement si les offsets coïncident ; sans remplacement, chaque ligne reçoit seulement un edit de marge. Les helpers conservent marqueurs, style de ligne, plafond de lignes vides et structure de tags selon les options. Deux limites ressortent : les actions d’espacement prepend ne sont pas exécutées sur un hidden et une première ligne de corps blanche rend le helper multiligne non idempotent.

**Limite de preuve de ce périmètre.** Lecture statique uniquement ; aucune exécution du projet ni mesure de coût.

Comparaison incluse : [LSP-037](#comparison-lsp-037).

<a id="finding-lsp-format-keyword-casse"></a>

### LSP-FORMAT-KEYWORD-CASSE — Les sélecteurs de formatting perdent les variantes de casse de ikw

**P2 · défaut · confiance forte.** Périmètres : CST-005, EXA-007, EXA-014, EXA-020, EXA-027, LSP-035.

**Mécanisme identifié.** formatter.keyword/keywords passe par find_nodes_for_keyword puis spells_keyword, qui exige cst.text(id)==keyword. La sélection compare donc la variante écrite dans le document au texte demandé par le formatter, même quand le littéral de grammaire est insensible à la casse. Langium sélectionne par grammarSource.value.

**Condition à vérifier.** Une grammaire ikw<"module"> et la règle keyword("module").append(oneSpace), avec la source MODULE   M. La variante est syntaxiquement valide mais la région du mot-clé est vide.

**Impact.** Espacements et sauts de ligne des mots structurants non normalisés selon la casse choisie ; les quatre exemples utilisent ikw et des sélecteurs de mots en casse fixe.

**Direction de correction.** Sélectionner les mots-clés par identité/valeur du littéral de grammaire, ou respecter son indicateur de casse au lieu de comparer seulement les octets source.

**Sources à relire.**

- [src/lsp/cursiv/formatting/AbstractFormatter.hpp:116](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/formatting/AbstractFormatter.hpp#L116) — Appel du collecteur de mots-clés.
- [src/lsp/cursiv/formatting/AbstractFormatter.cpp:28](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/formatting/AbstractFormatter.cpp#L28) — Délégation à CstUtils.
- [src/core/cursiv/syntax-tree/CstUtils.cpp:111](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/syntax-tree/CstUtils.cpp#L111) — Égalité textuelle exacte.
- [examples/arithmetics/src/core/arithmetics/ArithmeticsGrammar.hpp:8](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/arithmetics/src/core/arithmetics/ArithmeticsGrammar.hpp#L8) — module déclaré ikw.
- [examples/arithmetics/src/lsp/arithmetics/ArithmeticsFormatter.cpp:7](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/arithmetics/src/lsp/arithmetics/ArithmeticsFormatter.cpp#L7) — Règle sur module en minuscules.
- [langium/packages/langium/src/utils/grammar-utils.ts:213](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/utils/grammar-utils.ts#L213) — Langium compare la valeur du Keyword de grammaire.

**Contexte CST-005 — Navigation parents, enfants et voisinage.** Les recherches par feature et par mot-clé appliquent volontairement un ordre différent entre reconnaissance du record et frontière de propriétaire AST : une écriture de membre peut être le record qui ouvre le nœud enfant, tandis qu’un mot-clé du nœud enfant ne doit pas être volé par son parent. Le parcours recule par sous-arbre sans récursion puis inverse les résultats pour l’ordre source. find_leaf_before_offset ignore trivia, réparations et étendues vides ; word_at traite séparément la fin d’un mot et son début. L’index de feature hors limites est explicitement rabattu sur le dernier résultat. Le raccord formatting révèle cependant que spells_keyword compare le texte source au littéral avec égalité sensible à la casse. Une variante de ikw est reconnue par le parseur mais n’est pas retrouvée par keyword("littéral"); le constat partagé est porté par LSP-035.

**Limite de preuve de ce périmètre.** Ces recherches supposent les subtreeSize et fins monotones du builder. La comparaison établit les conventions inspectées sans couvrir toutes les combinaisons de CST réparés.

Comparaison incluse : [CST-005](#comparison-cst-005).

**Contexte EXA-007 — Mise en forme des expressions, appels et commentaires.** Le formatter impose espaces autour de l’opérateur via sa propriété AST, resserre parenthèses/virgules et protège les branches binaires ou groupées incomplètes. Les listes vides ne contiennent grammaticalement pas de parenthèses ; leur absence de règle spécifique est donc cohérente. Chaque statement commence une ligne ; la valeur après : peut rester sur la ligne ou être indentée. Seuls les commentaires multilignes sont remplacés, via le helper commun. Aucun changement de token métier n’apparaît dans les règles propres à l’exemple. Relecture transversale : les sélecteurs de mots-clés de ces règles dépendent de la casse écrite ; voir LSP-FORMAT-KEYWORD-CASSE (LSP-035). Le helper multiligne utilisé pour les commentaires /** est aussi concerné par LSP-FORMAT-COMMENT-IDEMPOTENCE.

**Limite de preuve de ce périmètre.** Idempotence globale et résolution des conflits d’édition dépendent du moteur de formatting commun ; aucune exécution.

**Contexte EXA-014 — Mise en forme des blocs et commentaires de documentation.** Les déclarations racines perdent leur indentation ; chaque package/entité utilise le même helper de bloc. extends ne reçoit une règle que si sa référence est écrite, many seulement si le flag est présent, et le deux-points est resserré avant/espacé après. Les commentaires /** sont distingués de /* ; les marqueurs et préfixes de ligne des commentaires de documentation sont transmis explicitement, avec reconnaissance de tags @. Les line comments ont leur helper dédié. Aucun défaut local de sélection ou de conservation des marqueurs trouvé. Relecture transversale : les sélecteurs de mots-clés de ces règles dépendent de la casse écrite ; voir LSP-FORMAT-KEYWORD-CASSE (LSP-035). Le helper multiligne utilisé pour les commentaires /** est aussi concerné par LSP-FORMAT-COMMENT-IDEMPOTENCE.

**Limite de preuve de ce périmètre.** Stabilité du helper de blocs/commentaires et arbitrage des edits relèvent du moteur commun ; pas d’exécution.

**Contexte EXA-020 — Mise en forme coordonnée des deux modèles.** Chaque langage possède son formatter, sélectionné par la fonction de création correspondante. Contact et ponctuation gardent les mêmes espaces ; les groupes racines ajoutent un saut de ligne à partir du second élément écrit. Les clauses applicable for utilisent les mêmes conventions, et le formatter des tests traite toutes les virgules de ses deux listes. Les chaînes ne sont pas modifiées. testFile.empty sert de proxy de présence : une clause explicitement écrite testfile="" garde son texte mais ne reçoit pas la règle d’espacement spécifique à cette clause ; c’est une limite de normalisation du style, sans perte de syntaxe démontrée. Relecture transversale : les sélecteurs de mots-clés de ces règles dépendent de la casse écrite ; voir LSP-FORMAT-KEYWORD-CASSE (LSP-035).

**Limite de preuve de ce périmètre.** Aucune exécution de formatting ; idempotence/arbitrage des edits relèvent du moteur commun.

**Contexte EXA-027 — Mise en forme des sections, états et actions.** Les handlers sont installés pour machine, état et transition. Les sections optionnelles events/commands ne reçoivent leurs règles que lorsqu’elles ont des membres ; chaque élément est indenté, initialState commence une ligne et les états sont séparés par deux sauts. Les actions restent en bloc inline, le premier espace étant fourni par la brace et les suivants par slice(1), ce qui évite un double contrôle du même gap. Un état sans transition reste sur une seule ligne avec end ; les transitions sont indentées et => espacé. Les références ou tokens absents d’un CST partiel sélectionnent des régions vides via le moteur commun. Relecture transversale : les sélecteurs de mots-clés de ces règles dépendent de la casse écrite ; voir LSP-FORMAT-KEYWORD-CASSE (LSP-035).

**Limite de preuve de ce périmètre.** Aucun effet destructeur local établi ; stabilité et arbitrage des edits dépendent du moteur commun, non exécuté ici.

**Contexte LSP-035 — Sélection des régions et actions.** NodeFormatter rattache les actions à des IDs CST et à un côté prepend/append ; les pointeurs de membres sont contraints à la famille du type AST. property(index) serre l’index hors borne sur la dernière occurrence, tandis que keyword(index) choisit une région vide hors borne ; c’est une divergence de commodité avec Langium qui serre aussi l’index des keywords. properties/keywords concatènent les résultats par sélecteur, pas par tri global, comme Langium ; slice a la sémantique de bornes négatives et clamp de Array.slice. interior exige deux régions unitaires et le helper échange les bornes inversées. formatBlock distingue le bloc vide et applique l’indentation aux records intérieurs ; formatSeparatedList attache les deux côtés des séparateurs. fit réunit les mouvements triés mais remet les options à leur défaut, comme Langium. Le défaut concret est l’identification des mots-clés par leur orthographe source, incompatible avec les variantes de casse admises par ikw.

**Limite de preuve de ce périmètre.** Lecture statique uniquement ; aucune exécution du projet ni mesure de coût.

Comparaison incluse : [LSP-035](#comparison-lsp-035).

<a id="finding-lsp-format-plage-debut"></a>

### LSP-FORMAT-PLAGE-DEBUT — Un chevauchement sur le début de la sélection peut exclure tout un sous-arbre

**P2 · limite · confiance forte.** Périmètres : LSP-038.

**Mécanisme identifié.** inside_range couvre containment, inclusion complète et chevauchement de la fin de sélection, mais pas le cas span.start<selection.start<=span.end<selection.end. doDocumentFormat utilise ce prédicat pour élaguer les sous-arbres avant d’enregistrer leurs règles. Le contrat public parle pourtant des lignes qui se chevauchent.

**Condition à vérifier.** Bloc AST couvrant lignes0..5 et sélection lignes3..10 ; le bloc n’est pas la racine. inside_range retourne false et les descendants des lignes3..5 ne reçoivent pas leur callback.

**Impact.** Formatage incomplet de la portion initiale de sélection ; prolonger une sélection peut faire disparaître les règles d’un bloc auparavant formaté.

**Direction de correction.** Utiliser un test symétrique de chevauchement de lignes pour l’admission des sous-arbres, puis appliquer la politique d’extension voulue aux edits eux-mêmes.

**Sources à relire.**

- [src/lsp/cursiv/formatting/Formatter.hpp:29](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/formatting/Formatter.hpp#L29) — Contrat public de chevauchement de lignes.
- [src/lsp/cursiv/formatting/FormattingEngine.hpp:67](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/formatting/FormattingEngine.hpp#L67) — Prédicat asymétrique.
- [src/lsp/cursiv/formatting/AbstractFormatter.cpp:130](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/formatting/AbstractFormatter.cpp#L130) — Élagage du sous-arbre à partir de ce prédicat.
- [src/lsp/cursiv/formatting/FormattingEngine.cpp:94](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/formatting/FormattingEngine.cpp#L94) — Même prédicat pour les edits.
- [langium/packages/langium/src/lsp/formatter.ts:195](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/formatter.ts#L195) — Même asymétrie dans Langium.

**Contexte LSP-038 — Admission document, plage et frappe.** Le formatage document refuse tout diagnostic syntaxique ; les conversions et diagnostics de budget ne sont pas assimilés à une réparation de syntaxe. Le formatage de plage/frappe n’est admis que si la première erreur syntaxique se situe après la fin de plage. La frappe fabrique une sélection allant du début de la ligne au caret, mais le filtrage par lignes peut produire des edits après le caret ou traversant une borne : ce choix est explicitement documenté. Le serveur route les trois modes à Parsed et vérifie le caractère déclencheur du formatter courant après l’union des capacités de session. Les providers par défaut n’annoncent aucun trigger. Les options reçues sont transmises, avec tabSize borné par le moteur. Le prédicat d’intersection asymétrique peut toutefois ignorer un sous-arbre qui recouvre seulement le début de la plage.

**Limite de preuve de ce périmètre.** Lecture statique uniquement ; aucune exécution du projet ni mesure de coût.

Comparaison incluse : [LSP-038](#comparison-lsp-038).

<a id="finding-lsp-format-priorite-gap"></a>

### LSP-FORMAT-PRIORITE-GAP — La priorité ne départage pas deux règles visant le même gap par des côtés différents

**P2 · défaut · confiance forte.** Périmètres : LSP-036.

**Mécanisme identifié.** FormattingActions compare les priorités seulement pour une clé (NodeId,Prepend/Append). Une règle append sur le token gauche et une règle prepend sur le token droit occupent deux clés mais produisent le même span. finalize_edits élimine ensuite le premier edit sans conserver/comparer sa priorité.

**Condition à vérifier.** A.append(spaces(1,{priority:100})) et B.prepend(spaces(2,{priority:0})) sur deux tokens adjacents : l’edit de B, émis ensuite, remplace celui de A.

**Impact.** Une règle de priorité inférieure peut supplanter celle qui devait verrouiller l’espacement ; ordre du parcours et côté choisi deviennent plus forts que la priorité annoncée.

**Direction de correction.** Arbitrer les contraintes par frontière textuelle effective ou transmettre priorité et ordre de règle jusqu’à la fusion des edits.

**Sources à relire.**

- [src/lsp/cursiv/formatting/FormattingAction.hpp:28](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/formatting/FormattingAction.hpp#L28) — Contrat annoncé sur la priorité du gap.
- [src/lsp/cursiv/formatting/FormattingEngine.hpp:46](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/formatting/FormattingEngine.hpp#L46) — Clé distincte pour node et side.
- [src/lsp/cursiv/formatting/FormattingEngine.cpp:526](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/formatting/FormattingEngine.cpp#L526) — Comparaison limitée à une clé.
- [src/lsp/cursiv/formatting/FormattingEngine.cpp:645](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/formatting/FormattingEngine.cpp#L645) — Prepend du token droit.
- [src/lsp/cursiv/formatting/FormattingEngine.cpp:651](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/formatting/FormattingEngine.cpp#L651) — Append du token gauche lors de sa visite antérieure.
- [src/lsp/cursiv/formatting/FormattingEngine.cpp:475](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/formatting/FormattingEngine.cpp#L475) — Fusion sans information de priorité.

**Contexte LSP-036 — Arbitrage des espaces et retraits.** Le collecteur donne priorité au dernier enregistrement de priorité au moins égale pour une même clé. Les mouvements fit sont choisis d’après les retours de ligne déjà présents ; allowMore a priorité sur allowLess si les deux sont vrais. Les additions de niveaux et conversions de comptes sont contrôlées en int64, tabSize est borné à1..64, les comptes négatifs de chaînes sont saturés à0. Le style CRLF/LF/CR vient du premier terminateur du document ; les edits identiques sont comparés aux octets originaux sans suppression des CR. Le parcours CST et ses restaurations de niveaux sont itératifs ; le tri stable final élimine doublons exacts et chevauchements. Cela garantit une liste compatible pour le client, mais l’arbitrage n’est pas global par gap : une priorité forte peut être perdue entre append et prepend.

**Limite de preuve de ce périmètre.** Lecture statique uniquement ; aucune exécution du projet ni mesure de coût.

Comparaison incluse : [LSP-036](#comparison-lsp-036).

<a id="finding-lsp-semantic-legende-fixe"></a>

### LSP-SEMANTIC-LEGENDE-FIXE — Le provider ne peut pas déclarer ses propres catégories ou modificateurs sémantiques

**P2 · écart d’API · confiance forte.** Périmètres : LSP-039.

**Mécanisme identifié.** La légende est une fonction globale fixe de8types et aucun modifier. DefaultLanguageServer l’annonce pour chaque provider et l’interface SemanticTokenProvider n’offre que full/range, sans options ni getters de légende. Remplacer le provider seul ne permet donc pas d’annoncer function, parameter, namespace, readonly ou une catégorie propre au langage.

**Condition à vérifier.** Langage souhaitant une coloration sémantique spécialisée au-delà des8catégories génériques ou des modificateurs declaration/readonly/deprecated.

**Impact.** L’auteur doit rabattre ses catégories sur les8indices existants ou modifier le serveur/contrat global ; émettre de nouveaux indices directement rendrait la réponse incompatible avec la légende annoncée.

**Direction de correction.** Exposer les options et tables de légende au niveau provider, puis fusionner/valider les indices entre langues à initialize comme Langium.

**Sources à relire.**

- [src/lsp/cursiv/semantic/SemanticTokenProvider.hpp:18](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/semantic/SemanticTokenProvider.hpp#L18) — Enum de8indices.
- [src/lsp/cursiv/semantic/SemanticTokenProvider.hpp:34](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/semantic/SemanticTokenProvider.hpp#L34) — Interface sans légende/options.
- [src/lsp/cursiv/semantic/SemanticTokenProvider.cpp:12](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/semantic/SemanticTokenProvider.cpp#L12) — Légende globale fixe sans modifiers.
- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:355](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L355) — Le serveur force la légende globale.
- [langium/packages/langium/src/lsp/semantic-token-provider.ts:74](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/semantic-token-provider.ts#L74) — Provider Langium expose types/modifiers/options.
- [langium/packages/langium/src/lsp/semantic-token-provider.ts:83](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/semantic-token-provider.ts#L83) — Fusion avec détection de conflits d’indices.

**Contexte LSP-039 — Classification et légende.** Les8valeurs enum correspondent exactement aux8entrées de légende et le cinquième entier est toujours0, cohérent avec l’absence de modifiers. La classification générique donne Type aux déclarations possédant d’autres déclarations, Property aux autres noms, Variable aux références résolues ou non, Comment à tout hidden, puis string/number selon le premier caractère et keyword/operator selon le littéral. Ce sont des heuristiques documentées, pas une résolution sémantique de fonctions/paramètres/types. Les spans sont triés par début, largeur décroissante et priorité aux noms, puis les chevauchements sont abandonnés ; ce choix est déterministe. La spécialisation de légende reste impossible via le slot standard et la classe générique n’expose pas de hook de classification par nœud.

**Limite de preuve de ce périmètre.** Lecture statique uniquement ; aucune exécution du projet ni mesure de coût.

Comparaison incluse : [LSP-039](#comparison-lsp-039).

<a id="finding-ws-cac-deferred-payload"></a>

### WS-CAC-DEFERRED-PAYLOAD — La rétention des valeurs retirées n’est pas bornée par le seuil de 8 Mio

**P2 · limite · confiance forte.** Périmètres : CAC-008.

**Mécanisme identifié.** Le seuil de taille compte uniquement la table, pas les Nodes ni les allocations détenues par V. Un petit nombre de tables contenant de grosses valeurs peut rester retiré sans prochaine collecte ; un guard long d’un autre cache retarde aussi leur éligibilité.

**Condition à vérifier.** Soit moins de 32 retraits ET moins de 8 Mio de tables comptées depuis la dernière collecte malgré des payloads volumineux ; soit une collecte retardée par un lecteur ancien, qui sort ensuite sans qu’une nouvelle collecte ait lieu.

**Impact.** La mémoire reste détenue sans délai de restitution garanti ; ce mécanisme est distinct de la fuite irréversible de CAC-007.

**Direction de correction.** Exposer une politique de collecte explicite de production ou un drainage après quiescence et comptabiliser les payloads si une borne mémoire est annoncée.

**Sources à relire.**

- [src/core/cursiv/utils/ConcurrentTable.cpp:29](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/utils/ConcurrentTable.cpp#L29) — Seuils fixes.
- [src/core/cursiv/utils/ConcurrentTable.hpp:264](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/utils/ConcurrentTable.hpp#L264) — allocated ignore les entrées séparées.
- [src/core/cursiv/utils/ConcurrentTable.cpp:181](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/utils/ConcurrentTable.cpp#L181) — Collecte seulement au retrait/seuil ou appel explicite.

**Contexte CAC-008 — Collecte globale des tables retirées.** La collecte ne retire que les epochs strictement antérieures au plus ancien lecteur actif. Elle suspend la libération si membarrier échoue, copie le lot avant de retirer les entrées et conserve la deque si due.assign manque de mémoire. Les destructeurs s’exécutent hors du mutex global, mais peuvent encore être appelés sous le mutex du clear initiateur. Les déclencheurs portent sur 32 retraits ou 8 Mio des seules allocations de tables ; aucun déclenchement à la sortie du dernier guard n’est installé.

**Limite de preuve de ce périmètre.** Rétention structurelle, pas fuite par elle-même. Aucun effet mesuré ni défaut d’ordre mémoire affirmé ; les destructeurs réentrants arbitraires demandent un contrat propre.

<a id="finding-ws-cac-functor-contract"></a>

### WS-CAC-FUNCTOR-CONTRACT — Les caches imposent des contraintes non exprimées aux hashers personnalisés

**P2 · écart d’API · confiance forte.** Périmètres : CAC-001.

**Mécanisme identifié.** Hash{} est recréé pour chaque opération et son appel est placé dans mixed noexcept. Un hasher dont chaque instance possède un sel différent peut rendre une clé introuvable ; un constructeur ou appel lançant termine le processus. KeyEqual est également recréé. Les templates publics exposent ces paramètres sans formaliser ces exigences supplémentaires.

**Condition à vérifier.** Un utilisateur fournit à SimpleCache/DocumentCache un functor à état par instance ou susceptible de lever ; les hashers livrés relus sont stables et non lançants.

**Impact.** Mémoïsation incohérente ou terminaison à travers une API générique ; pas de défaut démontré pour les hashers livrés.

**Direction de correction.** Conserver les functors par instance, ou contraindre et documenter explicitement stabilité interinstances et absence d’exception ; garder la migration non lançante.

**Sources à relire.**

- [src/core/cursiv/utils/ConcurrentTable.hpp:451](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/utils/ConcurrentTable.hpp#L451) — Construction et appel du Hash dans mixed noexcept.
- [src/core/cursiv/utils/ConcurrentTable.hpp:509](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/utils/ConcurrentTable.hpp#L509) — Rehash des clés inline scalaires pendant migration.
- [src/core/cursiv/utils/SimpleCache.hpp:16](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/utils/SimpleCache.hpp#L16) — Paramètres publics Hash/KeyEqual.

**Contexte CAC-001 — Lecture d’une table concurrente.** find garde une epoch jusqu’à la copie de V. lookup filtre d’abord par tag, puis hash conservé lorsque nécessaire, puis KeyEqual ; ready acquire interdit la lecture d’un payload dont le constructeur n’a pas terminé. Un slot réservé mais non prêt peut être omis par une lecture simultanée, compatible avec l’insertion encore en cours. Les probes sont bornées par le nombre de groupes. Les functors ne sont toutefois pas conservés : Hash et KeyEqual sont recréés à chaque comparaison, et mixed est noexcept.

**Limite de preuve de ce périmètre.** Pas d’instanciation de hasher adversarial ; la conséquence résulte directement des appels et de noexcept.

<a id="finding-ws-cac-retire-ownership"></a>

### WS-CAC-RETIRE-OWNERSHIP — Une exception d’enregistrement du retrait perd définitivement l’ancienne table

**P2 · défaut · confiance forte.** Périmètres : CAC-007.

**Mécanisme identifié.** Le pointeur retiré n’est plus courant mais retired.push_back peut allouer et lever avant d’acquérir son ownership. Ni RAII ni liste de secours ne conservent ce pointeur pour une collecte ultérieure.

**Condition à vérifier.** Échec d’allocation lors du push_back de retire après un clear ou une publication de croissance réussie.

**Impact.** Fuite permanente de l’ancienne table ; clear peut aussi perdre ses Nodes et les ressources détenues par leurs valeurs, aggravant la pression mémoire.

**Direction de correction.** Préparer un enregistrement possédé avant le commit ou rendre le transfert de retrait non allouant. Ne pas libérer immédiatement un pointeur encore accessible aux anciens lecteurs.

**Sources à relire.**

- [src/core/cursiv/utils/ConcurrentTable.hpp:326](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/utils/ConcurrentTable.hpp#L326) — Ancienne table remise à retire après échange.
- [src/core/cursiv/utils/ConcurrentTable.hpp:611](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/utils/ConcurrentTable.hpp#L611) — Retrait après CAS gagnant.
- [src/core/cursiv/utils/ConcurrentTable.cpp:179](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/utils/ConcurrentTable.cpp#L179) — push_back pouvant lever avant enregistrement.

**Contexte CAC-007 — Transfert d’ownership à la file des retraits.** clear et publish ont déjà détaché l’ancienne table lorsqu’ils appellent retire. retire avance l’epoch puis push_back dans une deque ; aucun guard propriétaire ne couvre l’ancienne table et aucun mécanisme de secours ne l’enregistre si push_back échoue. La protection de due.assign dans collect_locked est postérieure et ne traite pas cette fenêtre. Après un clear, l’allocation et ses Nodes deviennent inaccessibles ; après croissance, l’ancienne allocation est perdue, les Nodes restant possédés par le successeur.

**Limite de preuve de ce périmètre.** Fenêtre d’exception confirmée statiquement ; aucune injection de bad_alloc. L’échec distinct de due.assign est correctement conservateur.

<a id="finding-ws-cac-slot-exhaustion"></a>

### WS-CAC-SLOT-EXHAUSTION — Les 1024 inscriptions TLS peuvent bloquer un nouvel utilisateur du cache

**P2 · limite · confiance forte.** Périmètres : CAC-006.

**Mécanisme identifié.** Un slot reste réservé pour la vie du thread après son premier accès, même idle. Après 1024 threads encore vivants, takeSlot parcourt indéfiniment les CAS puis yield jusqu’à une fin de thread.

**Condition à vérifier.** Hôte embarquant plusieurs pools persistants ou beaucoup de threads utilisateurs ; 1024 threads inscrits restent vivants et attendent un travail confié à un nouveau thread.

**Impact.** Le nouvel accès n’a aucune borne de progression et peut participer à une attente permanente ; ce plafond est partagé par toutes les instances de cache.

**Direction de correction.** Prévoir enregistrement extensible, restitution au guard extérieur ou échec contrôlé publiquement documenté ; agrandir seulement la constante ne supprime pas la limite.

**Sources à relire.**

- [src/core/cursiv/utils/ConcurrentTable.cpp:24](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/utils/ConcurrentTable.cpp#L24) — 1024 slots.
- [src/core/cursiv/utils/ConcurrentTable.cpp:129](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/utils/ConcurrentTable.cpp#L129) — Restitution à la fin du thread.
- [src/core/cursiv/utils/ConcurrentTable.cpp:144](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/utils/ConcurrentTable.cpp#L144) — Boucle infinie sans timeout ni token.

**Contexte CAC-006 — Enregistrement des lecteurs par epoch.** Le guard extérieur enregistre l’epoch et les guards imbriqués incrémentent seulement depth. À sa sortie, le slot devient idle ; taken n’est rendu qu’au destructeur TLS, ou immédiatement pour les accès effectués après ce destructeur. Les slots sont globaux à toutes les tables et leur tableau a 1024 entrées. takeSlot ne fournit ni croissance ni issue d’échec : un thread supplémentaire boucle avec yield même si les détenteurs ne lisent plus rien. Le premier accès au State alloue aussi dans une chaîne Guard noexcept.

**Limite de preuve de ce périmètre.** Conséquences de contrôle établies par lecture ; aucune saturation de threads ni panne d’allocation provoquée.

<a id="finding-ws-lsp-action-documentchanges-overwrite"></a>

### WS-LSP-ACTION-DOCUMENTCHANGES-OVERWRITE — Le versionnement écrase les documentChanges fournis par une action

**P2 · défaut · confiance forte.** Périmètres : LSP-041.

**Mécanisme identifié.** stampVersion est appliqué à toutes les actions lorsqu’un client accepte documentChanges. Dès que changes est présent, la fonction reconstruit une liste depuis ce champ puis assigne action.edit->documentChanges sans examiner une liste préexistante. Des opérations create/rename/delete ou des edits annotés/versionnés peuvent ainsi disparaître.

**Condition à vérifier.** Provider personnalisé renvoyant changes et documentChanges, notamment une représentation de repli pour anciens clients ; client supportant les documentChanges.

**Impact.** Le client reçoit le repli réécrit à la place de la représentation qu’il devait préférer, avec perte d’opérations de fichier ou d’ordre d’édition.

**Direction de correction.** Conserver une liste documentChanges déjà fournie ; ne promouvoir changes que si cette liste est absente. Si les deux champs sont interdits par l’API Cursiv, le déclarer et le valider au lieu de l’altérer.

**Sources à relire.**

- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:829](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L829) — Condition ne vérifiant que changes.
- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:848](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L848) — Écrasement de documentChanges.
- [deps/lsp-framework/lspgen/meta_model/metaModel_3.18.json:3575](https://github.com/ydaveluy/lsp-framework/blob/10d5d25721b22e06252f38c9fa93bf91dd702355/lspgen/meta_model/metaModel_3.18.json#L3575) — Contrat de préférence pour documentChanges quand présent.

**Contexte LSP-041 — Actions issues des diagnostics et versions.** Les quick-fixes sont construits depuis les diagnostics actuels dans la plage demandée, pas depuis des offsets client périmés. Chaque payload est contrôlé sur type, révision et bornes ; le diagnostic client n’est repris que s’il correspond au diagnostic courant. Les actions personnalisées sont ajoutées après le filtre de quick-fix. Le coût de spelling_actions est suivi en VAL-005. Le versionnement global de toutes les actions peut toutefois écraser un WorkspaceEdit déjà complet.

**Limite de preuve de ce périmètre.** Audit statique uniquement ; la logique métier des providers optionnels dépend du langage qui les installe.

Comparaison incluse : [LSP-041](#comparison-lsp-041).

<a id="finding-ws-lsp-client-result-only"></a>

### WS-LSP-CLIENT-RESULT-ONLY — Les requêtes sortantes n’exposent ni erreur ni terminaison garantie

**P2 · écart d’API · confiance forte.** Périmètres : LSP-009.

**Mécanisme identifié.** LanguageClient::configuration et sendRequest ne fournissent qu’un callback de succès. Une erreur client, une disparition de session ou une absence de réponse ne déclenche aucun callback ; aucun jeton de requête/annulation n’est retourné. Cette limite est annoncée par le contrat.

**Condition à vérifier.** Extension Cursiv attendant un résultat client pour libérer une ressource ou faire progresser son état.

**Impact.** L’intégrateur doit créer son propre timeout/abandon ; ConfigurationProvider peut rester isReady()==false après une requête acceptée mais jamais satisfaite.

**Direction de correction.** Offrir une issue résultat/erreur/annulation, ou un handle annulable et documenter la responsabilité de timeout.

**Sources à relire.**

- [src/lsp/cursiv/runtime/LanguageClient.hpp:43](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/LanguageClient.hpp#L43) — Callback non appelé en cas d’erreur ou silence.
- [src/lsp/cursiv/runtime/LanguageClient.hpp:52](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/LanguageClient.hpp#L52) — Même contrat pour sendRequest.
- [src/lsp/cursiv/workspace/DefaultConfigurationProvider.cpp:149](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/workspace/DefaultConfigurationProvider.cpp#L149) — Ready faux jusqu’au callback.

**Contexte LSP-009 — Requêtes sortantes et callbacks client.** Le client sortant conserve paramètres et résultats par valeur et remet les callbacks dans NotificationQueue ; ils ne mutent pas le workspace depuis le thread réseau. Le garde de durée de vie maintient queue/client jusqu’à l’arrêt de l’endpoint. L’interface documente toutefois des callbacks uniquement de succès, ce qui limite la gestion d’erreurs côté utilisateur.

**Limite de preuve de ce périmètre.** Analyse statique des sources et contrats ; aucune exécution du projet.

<a id="finding-ws-lsp-completion-trigger-context"></a>

### WS-LSP-COMPLETION-TRIGGER-CONTEXT — Le provider de complétion perd triggerKind et triggerCharacter

**P2 · écart d’API · confiance forte.** Périmètres : LSP-023.

**Mécanisme identifié.** CompletionProvider::getCompletion reçoit Document, Position et CancellationToken. DefaultLanguageServer abandonne params.context avant cet appel ; un provider ne peut donc distinguer invocation explicite, caractère déclencheur ou rafraîchissement d’une liste incomplète.

**Condition à vérifier.** Une langue souhaite des propositions spécifiques après un déclencheur ou une politique différente pour Ctrl+Espace et les demandes automatiques.

**Impact.** Portage d’un provider Langium nécessitant une surcharge du serveur ou une modification d’interface ; la configuration triggerCharacters ne suffit pas à restituer le contexte.

**Direction de correction.** Transmettre CompletionParams ou au moins son CompletionContext aux providers et aux hooks de contexte.

**Sources à relire.**

- [src/lsp/cursiv/completion/CompletionProvider.hpp:31](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/completion/CompletionProvider.hpp#L31) — Signature réduite.
- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:1133](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L1133) — params.context non transmis.
- [langium/packages/langium/src/lsp/completion/completion-provider.ts:117](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/completion/completion-provider.ts#L117) — Langium reçoit CompletionParams.

**Contexte LSP-023 — Contexte et reprise au curseur.** Le contexte distingue remplacement du token actuel, insertion après token et reprise d’une règle de données. Les offsets internes sont calculés via TextDocument ; les insert/replace ranges sont limités à la ligne du caret. La complétion est exclue strictement à l’intérieur des trivia/recovered leaves. Les hooks disposent de l’identifiant et du token d’annulation, mais jamais du motif de la requête LSP.

**Limite de preuve de ce périmètre.** Lecture statique de l’implémentation, des contrats et des callers ; aucun test ou code projet exécuté.

Comparaison incluse : [LSP-023](#comparison-lsp-023).

<a id="finding-ws-lsp-folding-cancel-poll"></a>

### WS-LSP-FOLDING-CANCEL-POLL — Le parcours de repliement ne sonde l’annulation que lors des résultats

**P2 · limite · confiance forte.** Périmètres : LSP-032.

**Mécanisme identifié.** collectFolding et collectCommentFolding ne reçoivent pas le token. getFoldingRanges le vérifie au début et dans l’accepteur seulement ; des milliers de nœuds mono/biligne ou filtrés peuvent être parcourus sans aucun point d’arrêt.

**Condition à vérifier.** Grand document dont peu ou aucune région atteint le seuil de repliement, ou hooks de sélection coûteux.

**Impact.** Annulation tardive et verrou de lecture conservé pendant le parcours complet, même si le client a abandonné.

**Direction de correction.** Transmettre le token aux boucles et sonder périodiquement indépendamment de l’émission d’un résultat.

**Sources à relire.**

- [src/lsp/cursiv/ranges/DefaultFoldingRangeProvider.cpp:30](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/ranges/DefaultFoldingRangeProvider.cpp#L30) — Sonde située dans l’accepteur.
- [src/lsp/cursiv/ranges/DefaultFoldingRangeProvider.cpp:55](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/ranges/DefaultFoldingRangeProvider.cpp#L55) — Boucle sans token.
- [src/lsp/cursiv/ranges/DefaultFoldingRangeProvider.cpp:97](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/ranges/DefaultFoldingRangeProvider.cpp#L97) — Balayage CST sans token.

**Contexte LSP-032 — Repliement des objets et commentaires.** Le parcours des objets respecte shouldProcess et l’élagage shouldProcessContent ; la racine n’est pas repliée comme un objet. Les commentaires sont collectés sur l’ensemble du CST, y compris les trivia externes. Le seuil de trois lignes et la conservation visible du délimiteur final suivent le contrat. Les points d’annulation, en revanche, sont attachés à l’émission de ranges et ne bornent pas le parcours d’un document ne produisant aucun pli.

**Limite de preuve de ce périmètre.** Lecture statique de l’implémentation, des contrats et des callers ; aucun test ou code projet exécuté.

Comparaison incluse : [LSP-032](#comparison-lsp-032).

<a id="finding-ws-lsp-hierarchy-item-identity"></a>

### WS-LSP-HIERARCHY-ITEM-IDENTITY — Le helper de hiérarchie ne préserve pas l’identité après une édition

**P2 · écart d’API · confiance forte.** Périmètres : LSP-049, LSP-050.

**Mécanisme identifié.** hierarchy_node_of_item ne reçoit que l’URI et une position. Il redétermine les déclarations dans le texte courant et choisit un nom au même offset ou le plus petit nœud englobant ; il ne compare ni version, ni nom, ni identifiant transporté dans item.data.

**Condition à vérifier.** Le client conserve un item de hiérarchie puis le document est modifié avant incoming/outgoing/supertypes/subtypes ; une autre déclaration occupe la position conservée.

**Impact.** Le helper peut retourner une autre cible logique au lieu d’invalider l’item, bien qu’il évite un pointeur C++ pendant.

**Direction de correction.** Proposer un helper prenant un identifiant durable et une révision dans item.data, ou imposer explicitement l’invalidation des items lors d’édition et faire vérifier cette identité par le provider.

**Sources à relire.**

- [src/lsp/cursiv/hierarchy/HierarchyProviderSupport.cpp:32](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/hierarchy/HierarchyProviderSupport.cpp#L32) — Reconstruction seulement via URI/position.
- [src/lsp/cursiv/hierarchy/HierarchyProviderSupport.cpp:46](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/hierarchy/HierarchyProviderSupport.cpp#L46) — Choix de la cible courante.

**Contexte LSP-049 — Préparation et parcours des appels.** La préparation peut renvoyer une entrée par cible ; les expansions routent selon l’URI de l’item après l’index workspace. Le helper recherche d’abord un nom commençant à selectionRange.start, puis la cible locale la plus étroite, ce qui traite les ensembles surchargés sans garder de pointeur entre requêtes. Cette absence de pointeur n’assure pas l’identité logique d’un item après édition.

**Limite de preuve de ce périmètre.** Audit statique uniquement ; la logique métier des providers optionnels dépend du langage qui les installe.

Comparaison incluse : [LSP-049](#comparison-lsp-049).

**Contexte LSP-050 — Préparation et parcours des types.** La hiérarchie de types utilise les mêmes primitives de ciblage que celle d’appels et la même phase workspace IndexedReferences. supertypes/subtypes reçoivent l’item par valeur logique, sans accès direct à un ancien AST. Le helper n’est pas un registre d’identités persistantes ; la réserve après édition est la même que pour les appels.

**Limite de preuve de ce périmètre.** Audit statique uniquement ; la logique métier des providers optionnels dépend du langage qui les installe.

Comparaison incluse : [LSP-050](#comparison-lsp-050).

<a id="finding-ws-lsp-initialize-flag"></a>

### WS-LSP-INITIALIZE-FLAG — Les notifications personnalisées passent après un initialize échoué

**P2 · défaut · confiance forte.** Périmètres : LSP-007.

**Mécanisme identifié.** Session.initialized devient true avant server.initialize(params). Si celui-ci lève, l’endpoint reste Uninitialized et peut refuser les requêtes standard/custom, mais le garde de notification personnalisée ne lit que ce booléen et shutdownRequested.

**Condition à vérifier.** initialize lève après l’activation du booléen ; le client envoie ensuite une notification personnalisée.

**Impact.** Exécution de code applicatif avant une initialisation réussie, contrairement au commentaire de contrat du runtime.

**Direction de correction.** Ne valider le booléen qu’après réussite de server.initialize ou utiliser l’état actif de l’endpoint pour toutes les notifications personnalisées.

**Sources à relire.**

- [src/lsp/cursiv/runtime/LanguageServerRuntime.cpp:324](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/LanguageServerRuntime.cpp#L324) — Flag avant appel potentiellement levant.
- [src/lsp/cursiv/runtime/LanguageServerRuntime.cpp:341](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/LanguageServerRuntime.cpp#L341) — Appel initialize.
- [src/lsp/cursiv/runtime/LanguageServerRuntime.cpp:905](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/LanguageServerRuntime.cpp#L905) — Garde fondé sur le flag.
- [deps/lsp-framework/lsp/endpoint_base.cpp:264](https://github.com/ydaveluy/lsp-framework/blob/10d5d25721b22e06252f38c9fa93bf91dd702355/lsp/endpoint_base.cpp#L264) — L’endpoint active son état seulement après succès.

**Contexte LSP-007 — Handlers personnalisés.** Les méthodes personnalisées copient leurs handlers dans le runtime, déballent uniquement un tableau d’un élément et conservent les autres formes JSON. Les requests passent explicitement par preMethodCall. Les notifications utilisent un booléen local de session, et ce booléen est validé trop tôt si initialize échoue.

**Limite de preuve de ce périmètre.** Analyse statique des sources et contrats ; aucune exécution du projet.

<a id="finding-ws-lsp-inline-cancellation"></a>

### WS-LSP-INLINE-CANCELLATION — Le mode sans workers ne peut pas traiter l’annulation pendant une requête

**P2 · limite · confiance forte.** Périmètres : LSP-005.

**Mécanisme identifié.** Avec defaultWorkerCount()==0, le handler s’exécute sur la boucle de lecture et reçoit kNoCancel. Celle-ci ne peut lire $/cancelRequest tant que le handler ne rend pas la main.

**Condition à vérifier.** Hôte configuré sans workers, requête longue ou attendant une notification en cours.

**Impact.** Pas d’annulation client réactive dans ce mode ; une requête lente bloque aussi la réception des messages suivants.

**Direction de correction.** Documenter ce contrat de mode synchrone ; proposer une boucle séparée si l’hôte possède au moins un thread de lecture.

**Sources à relire.**

- [src/lsp/cursiv/runtime/LanguageServerRuntime.cpp:303](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/LanguageServerRuntime.cpp#L303) — Appel direct non annulable.
- [src/lsp/cursiv/runtime/LanguageServerRuntime.cpp:972](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/LanguageServerRuntime.cpp#L972) — Politique zéro worker documentée.

**Contexte LSP-005 — Annulation et erreurs de requête.** En mode pool, le token du RequestContext est capturé avant sortie de la boucle ; l’attente de notification et le handler le reçoivent. OperationCancelled devient RequestCancelled et un token arrêté après le calcul empêche de renvoyer un résultat périmé. Le mode zéro worker est différent et explicitement choisi par le runtime.

**Limite de preuve de ce périmètre.** Analyse statique des sources et contrats ; aucune exécution du projet.

<a id="finding-ws-lsp-json-precision"></a>

### WS-LSP-JSON-PRECISION — Les payloads JSON int64 perdent leur précision au passage LSP

**P2 · limite · confiance forte.** Périmètres : LSP-022.

**Mécanisme identifié.** to_lsp_any convertit tout entier hors int32 vers lsp::json::Decimal. Les entiers dont la magnitude dépasse 2^53 ne sont pas tous exactement représentables en double ; un identifiant porté par Diagnostic.data peut donc être changé avant son retour dans codeAction.

**Condition à vérifier.** JsonValue contient un int64 non exactement représentable en double, par exemple 9007199254740993.

**Impact.** Rupture de l’identité de données applicatives ou de la sélection d’un quick-fix ; l’affirmation du commentaire selon laquelle JSON ne distingue pas ces valeurs masque cette restriction.

**Direction de correction.** Conserver une représentation int64 côté JSON si le framework le permet, sinon rejeter/encoder ces valeurs en chaîne et documenter la plage exacte.

**Sources à relire.**

- [src/lsp/cursiv/actions/JsonBridge.cpp:18](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/actions/JsonBridge.cpp#L18) — Conversion int64 vers Decimal.
- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:1467](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L1467) — Le payload est annoncé comme rond-trip inchangé.

**Contexte LSP-022 — Conversion des données JSON métier.** Le pont conserve booléens, nulls, strings, arrays et objets par copie récursive. Les integers int32 restent entiers ; au-delà, le cast vers Decimal est une conversion vers double et ne préserve pas tous les int64. La restriction provient de la représentation numérique du framework mais devient observable dans le payload des diagnostics.

**Limite de preuve de ce périmètre.** Analyse statique des sources et contrats ; aucune exécution du projet.

<a id="finding-ws-lsp-notification-barrier"></a>

### WS-LSP-NOTIFICATION-BARRIER — La barrière de notifications attend le build complet

**P2 · limite · confiance forte.** Périmètres : LSP-004.

**Mécanisme identifié.** Une requête capturant le rang d’un didChange ne passe qu’après le retour de son callback. didChange exécute son écriture et le build synchrones ; atteindre la phase nécessaire au provider ne suffit donc pas pour libérer cette requête. La priorité Immediate du routeur n’élimine pas cette attente préalable.

**Condition à vérifier.** Notification didChange/didOpen antérieure à une requête, avec validation lente ou autre travail long dans le callback.

**Impact.** Complétion ou hover peuvent attendre le travail de validation restant malgré la disponibilité de leur phase documentaire.

**Direction de correction.** Séparer l’acceptation ordonnée de la mutation et l’achèvement du build, ou documenter ce coût de latence.

**Sources à relire.**

- [src/lsp/cursiv/runtime/LanguageServerRuntime.cpp:182](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/LanguageServerRuntime.cpp#L182) — Served seulement après callback.
- [src/lsp/cursiv/runtime/LanguageServerRuntime.cpp:279](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/LanguageServerRuntime.cpp#L279) — Attente avant call.
- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:1370](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L1370) — writeAndPublish englobe le build.

**Contexte LSP-004 — Barrière des notifications servies.** Le rang servi est avancé après chaque sortie normale ou exceptionnelle du callback. La requête attend donc toutes les notifications antérieures, et le stop réveille les attentes en annulation. Deux conséquences sont explicites dans le contrôle de flux : la barrière porte sur tout le callback, et ses exceptions sont absorbées sans canal de signalement.

**Limite de preuve de ce périmètre.** Analyse statique des sources et contrats ; aucune exécution du projet.

<a id="finding-ws-lsp-notification-error-sink"></a>

### WS-LSP-NOTIFICATION-ERROR-SINK — Les échecs des notifications sont perdus

**P2 · écart d’API · confiance forte.** Périmètres : LSP-004.

**Mécanisme identifié.** Le catch (...) de NotificationQueue ne journalise pas l’exception, ne déclenche pas de listener et marque la notification servie. Les exceptions d’une notification personnalisée ou d’un didChange hors capture locale sont ainsi invisibles aux intégrateurs.

**Condition à vérifier.** Un callback de notification lève ; il n’existe pas de mécanisme de signalement propre à ce callback.

**Impact.** Workspace potentiellement partiellement mis à jour et diagnostic opérationnel absent ; le client n’obtient aucun indice sur l’échec.

**Direction de correction.** Ajouter un sink d’erreurs ou un hook de journalisation, sans fabriquer de réponse JSON-RPC à une notification.

**Sources à relire.**

- [src/lsp/cursiv/runtime/LanguageServerRuntime.cpp:174](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/LanguageServerRuntime.cpp#L174) — Catch-all sans action.
- [src/lsp/cursiv/runtime/LanguageServerRuntime.cpp:182](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/LanguageServerRuntime.cpp#L182) — La barrière est avancée même après erreur.

**Contexte LSP-004 — Barrière des notifications servies.** Le rang servi est avancé après chaque sortie normale ou exceptionnelle du callback. La requête attend donc toutes les notifications antérieures, et le stop réveille les attentes en annulation. Deux conséquences sont explicites dans le contrôle de flux : la barrière porte sur tout le callback, et ses exceptions sont absorbées sans canal de signalement.

**Limite de preuve de ce périmètre.** Analyse statique des sources et contrats ; aucune exécution du projet.

<a id="finding-ws-lsp-pending-submit-exception"></a>

### WS-LSP-PENDING-SUBMIT-EXCEPTION — Une soumission de réponse échouée peut bloquer le shutdown

**P2 · défaut · confiance forte.** Périmètres : LSP-006.

**Mécanisme identifié.** pending.enter() est exécuté avant le retour de la tâche au dispatcher ; leave n’est créé qu’à l’entrée du corps. Si la copie du wrapper ou l’emplace de la queue du pool lève avant enqueue, la tâche est détruite sans décrément. pending.drain() n’a alors aucun chemin pour atteindre zéro.

**Condition à vérifier.** Exception d’allocation lors de la remise d’une réponse différée au pool, avant son insertion ; puis sortie après shutdown.

**Impact.** Blocage indéfini de la fin de session même si l’erreur de requête a été traduite par le dispatcher.

**Direction de correction.** Porter la réservation par un objet RAII capturé dont la destruction libère le compteur même si la tâche n’est jamais exécutée ; transférer ce jeton à la tâche sans double libération.

**Sources à relire.**

- [src/lsp/cursiv/runtime/LanguageServerRuntime.cpp:311](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/LanguageServerRuntime.cpp#L311) — LeaveOnExit seulement dans le corps.
- [src/lsp/cursiv/runtime/LanguageServerRuntime.cpp:314](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/LanguageServerRuntime.cpp#L314) — Incrément préalable.
- [deps/lsp-framework/lsp/message_handler.inl:171](https://github.com/ydaveluy/lsp-framework/blob/10d5d25721b22e06252f38c9fa93bf91dd702355/lsp/message_handler.inl#L171) — addTask peut échouer après enter.
- [deps/lsp-framework/lsp/thread_pool.cpp:51](https://github.com/ydaveluy/lsp-framework/blob/10d5d25721b22e06252f38c9fa93bf91dd702355/lsp/thread_pool.cpp#L51) — emplace avant exécution.
- [src/lsp/cursiv/runtime/LanguageServerRuntime.cpp:1001](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/LanguageServerRuntime.cpp#L1001) — Drain sur arrêt normal.

**Contexte LSP-006 — Pool de réponses et requêtes en vol.** Le garde LeaveOnExit équilibre enter pour tout corps de tâche effectivement exécuté, y compris en cas d’exception. En revanche, le compteur est incrémenté avant que l’endpoint ne transforme le résultat différé en tâche du pool. La dépendance contient des allocations sur ce trajet, sans rollback du compteur détenu par Cursiv.

**Limite de preuve de ce périmètre.** Analyse statique des sources et contrats ; aucune exécution du projet.

<a id="finding-ws-lsp-save-hooks"></a>

### WS-LSP-SAVE-HOOKS — Les hooks typés de sauvegarde sont absents de la surface serveur

**P2 · écart d’API · confiance forte.** Périmètres : LSP-053.

**Mécanisme identifié.** Cursiv ne propose ni didSave, ni willSave, ni willSaveWaitUntil dans LanguageServer et ne les annonce pas. Les méthodes personnalisées peuvent servir de contournement, mais leur ajout n’assemble pas automatiquement les capacités et les types de paramètres/résultats de synchronisation.

**Condition à vérifier.** Langue voulant valider à la sauvegarde ou calculer des TextEdit avant sauvegarde, notamment lors d’un portage Langium.

**Impact.** Adaptation du runtime/initialize nécessaire au-delà du remplacement d’un service ; fonctionnalité d’éditeur moins directement extensible.

**Direction de correction.** Ajouter des hooks optionnels typés et annoncer chaque option de synchronisation selon les hooks installés.

**Sources à relire.**

- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:273](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L273) — Sauvegarde volontairement non annoncée.
- [src/lsp/cursiv/runtime/LanguageServerRuntime.cpp:422](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/LanguageServerRuntime.cpp#L422) — Seulement open/change/close.
- [langium/packages/langium/src/lsp/language-server.ts:147](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/language-server.ts#L147) — Capacités Langium selon hooks.
- [langium/packages/langium/src/lsp/language-server.ts:337](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/language-server.ts#L337) — Enregistrement des trois hooks de sauvegarde.

**Contexte LSP-053 — Modes de synchronisation et contrat de sauvegarde.** La capacité annonce openClose et Incremental, correspondant aux trois handlers didOpen/didChange/didClose effectivement enregistrés. Les options save/willSave/willSaveWaitUntil restent absentes. Ce contrat est cohérent pour la synchronisation courante, mais la surface LanguageServer ne fournit aucun hook typé équivalent aux événements de sauvegarde Langium.

**Limite de preuve de ce périmètre.** Audit statique uniquement ; la logique métier des providers optionnels dépend du langage qui les installe.

Comparaison incluse : [LSP-053](#comparison-lsp-053).

<a id="finding-ws-lsp-service-requirement-extension"></a>

### WS-LSP-SERVICE-REQUIREMENT-EXTENSION — Le prérequis de phase est codé dans les wrappers du serveur

**P2 · écart d’API · confiance forte.** Périmètres : LSP-019.

**Mécanisme identifié.** Un provider personnalisé qui a besoin d’une phase plus tardive ne peut pas déclarer son prérequis dans son slot. DefaultLanguageServer choisit un état fixe pour chaque méthode ; modifier cette politique demande de surcharger les wrappers ou le serveur, alors que Langium expose requiredState lors de l’ajout des handlers.

**Condition à vérifier.** Portage d’un provider Langium demandant une autre phase, par exemple completion dépendant de validation ou outline dépendant des scopes.

**Impact.** Risque d’utiliser des données pas encore construites si seul le provider est remplacé ; adaptation plus intrusive du serveur.

**Direction de correction.** Rendre les prérequis configurables dans les services/registrations ou documenter explicitement les préconditions de chaque slot.

**Sources à relire.**

- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:928](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L928) — Phase fixe de documentSymbol.
- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:1130](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L1130) — Phase fixe de completion.
- [langium/packages/langium/src/lsp/language-server.ts:786](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/language-server.ts#L786) — Paramètre requiredState de createRequestHandler.

**Contexte LSP-019 — Prérequis de phase et langue du document.** Le routeur attend le scan puis la phase document ou workspace sans garder le read lock ; il retrouve le document, revérifie son état et utilise la langue liée par la factory. Les lectures workspace sont Normal afin d’éviter de voir un index en reconstruction ; les lectures locales sont Immediate après leur prérequis. Les wrappers déclarent leurs phases de façon cohérente avec leurs besoins, mais les phases ne sont pas configurables par provider dans l’API d’assemblage.

**Limite de preuve de ce périmètre.** Analyse statique des sources et contrats ; aucune exécution du projet.

Comparaison incluse : [LSP-019](#comparison-lsp-019).

<a id="finding-ws-lsp-session-reuse"></a>

### WS-LSP-SESSION-REUSE — État de session conservé lors de la réutilisation des services

**P2 · écart d’API · confiance forte.** Périmètres : LSP-002.

**Mécanisme identifié.** La fin de session retire LanguageClient mais ne vide ni le cache de configuration ni le registre _announced. Un nouvel initialize remet les capacités et dossiers à jour, sans réinitialiser ces deux données. Le nouveau client peut donc hériter de paramètres de l’ancien et ne pas recevoir un diagnostic déjà compté comme annoncé.

**Condition à vérifier.** Deux appels successifs à start_language_server avec le même SharedLspServices et le même DefaultLanguageServer ; documents ou configuration conservés.

**Impact.** Réutilisation difficile à prévoir pour un hôte embarqué ou une reconnexion ; paramètres et publications ne sont pas isolés par client.

**Direction de correction.** Documenter explicitement une instance par session, ou ajouter une réinitialisation de session pour configuration et annonces de diagnostics.

**Sources à relire.**

- [src/lsp/cursiv/runtime/LanguageServerRuntime.cpp:68](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/LanguageServerRuntime.cpp#L68) — Le garde retire le client en fin de session.
- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:223](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L223) — initialize ne vide pas _announced.
- [src/lsp/cursiv/workspace/DefaultConfigurationProvider.cpp:103](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/workspace/DefaultConfigurationProvider.cpp#L103) — Détachement : génération et ready, cache conservé.
- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:1423](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L1423) — Une annonce précédente peut supprimer une publication.

**Contexte LSP-002 — Démarrage et arrêt de session.** Le client est installé avant l’enregistrement des handlers ; queue et compteur survivent à l’endpoint et son pool. StopNotifications arrête la file avant destruction du pool ; LanguageClientGuard détache le provider de configuration avant de retirer le client. Le shutdown draine les notifications puis les requêtes. Cette durée de vie est cohérente pour une session, mais le contrat public ne précise pas le caractère réutilisable des services.

**Limite de preuve de ce périmètre.** Analyse statique des sources et contrats ; aucune exécution du projet.

<a id="finding-ws-lsp-workspace-folder-changes"></a>

### WS-LSP-WORKSPACE-FOLDER-CHANGES — Les changements de racines ne mettent pas à jour le workspace par défaut

**P2 · limite · confiance forte.** Périmètres : LSP-015.

**Mécanisme identifié.** DefaultLanguageServer ne répercute pas didChangeWorkspaceFolders sur WorkspaceManager/Documents et n’annonce pas changeNotifications. L’application doit surcharger le serveur et décider des ajouts/retraits/indexations.

**Condition à vérifier.** Client ajoutant ou retirant une racine après initialize ; besoin d’une session multi-root dynamique.

**Impact.** Le support initial de plusieurs racines ne suffit pas à suivre les changements de la fenêtre de l’éditeur.

**Direction de correction.** Documenter cette limite et fournir un handler de mise à jour des racines avec invalidation des documents concernés.

**Sources à relire.**

- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:446](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L446) — Annonce support initial.
- [src/lsp/cursiv/runtime/DefaultLanguageServer.hpp:71](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.hpp#L71) — Corps vide du handler.

**Contexte LSP-015 — Dossiers de workspace.** L’initialisation accepte workspaceFolders puis les replis rootUri/rootPath ; le client apprend supported=true. Le handler ultérieur de changement de dossiers existe mais son implémentation par défaut ne fait rien, et initialize n’annonce pas changeNotifications. La prise en charge est donc limitée aux racines initiales à moins de spécialiser le serveur.

**Limite de preuve de ce périmètre.** Analyse statique des sources et contrats ; aucune exécution du projet.

<a id="finding-ws-mcp-bound-language"></a>

### WS-MCP-BOUND-LANGUAGE — Les outils MCP retrouvent une langue par URI et perdent le languageId du document

**P2 · défaut · confiance forte.** Périmètres : MCP-009, MCP-013.

**Mécanisme identifié.** Les outils symbols et hover appellent serviceRegistry->findServices(home->uri) pour obtenir NameProvider ou DocumentationProvider. Ils ignorent home->language, fixé par la factory à partir de l’URI et du languageId. Lorsqu’un client impose une autre langue que celle déduite de l’extension, les services choisis ne décrivent pas l’AST cible.

**Condition à vérifier.** Plusieurs langues enregistrées ; document dont le languageId explicite diffère du routage par son URI, ou URI sans extension reconnue.

**Impact.** Nom/plage ou documentation erronés/absents ; un provider spécialisé peut recevoir un AST de la mauvaise langue.

**Direction de correction.** Utiliser home->language comme les providers LSP de définition, complétion et hover.

**Sources à relire.**

- [src/mcp/cursiv/DefaultMcpService.cpp:557](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/mcp/cursiv/DefaultMcpService.cpp#L557) — Recherche NameProvider par URI.
- [src/mcp/cursiv/DefaultMcpService.cpp:680](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/mcp/cursiv/DefaultMcpService.cpp#L680) — Recherche DocumentationProvider par URI.
- [src/core/cursiv/workspace/Document.hpp:41](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/Document.hpp#L41) — La langue attachée évite explicitement ce reroutage.

**Contexte MCP-009 — Recherche de symboles exportés.** symbols filtre par sous-chaîne avec casse ASCII, compte les correspondances et interrompt réellement le visiteur après 200 ; la réponse annonce alors les résultats élidés et suggère de préciser la requête. Les types et positions sont rendus par export. Le calcul de langue est divergent du document lié ; le fallback de plage suppose également un symbolId valide et ne protège pas les descriptions synthétiques sans CST.

**Limite de preuve de ce périmètre.** Analyse statique uniquement ; aucun nouvel outil MCP du projet exécuté pour cette fiche. Langium ne figure pas comme comparaison prévue pour ce périmètre.

**Contexte MCP-013 — Description d’un élément au curseur.** hover utilise le service References de la langue attachée au document source, conserve toutes les cibles de la déclaration et son span textuel, affiche leur tag de type AST et leur position de déclaration. Cette notion de type est celle de l’AST, pas un résultat universel d’inférence métier ; un langage peut remplacer l’outil pour cela. La documentation est demandée au document cible, ce qui est la bonne direction pour le multilangage, mais sa langue est reroutée par URI au lieu de lire home->language. Le cas est la même cause que celle déjà relevée dans symbols.

**Limite de preuve de ce périmètre.** Analyse statique uniquement ; aucun nouvel outil MCP, test ou reproducer exécuté. Les garanties des moteurs et du scoping sont examinées dans leurs propres périmètres.

<a id="finding-ws-mcp-catalog-duplicate"></a>

### WS-MCP-CATALOG-DUPLICATE — Le catalogue MCP peut publier plusieurs entrées de même nom mais n’en appeler qu’une

**P2 · écart d’API · confiance forte.** Périmètres : MCP-002.

**Mécanisme identifié.** registerTool/registerResource ajoutent toujours une nouvelle entrée. Les opérations list exposent les doublons, alors que tools/call/resources/read s’arrêtent sur la première correspondance. Une extension réenregistrant un outil de base ne le remplace donc pas et publie une définition potentiellement différente de celle exécutée.

**Condition à vérifier.** Service dérivé enregistrant un nom d’outil ou une URI de ressource déjà présent dans DefaultMcpService.

**Impact.** Catalogue ambigu pour le client et handler personnalisé inatteignable sans retirer l’entrée antérieure.

**Direction de correction.** Refuser les doublons, ou définir un remplacement atomique cohérent entre catalogue et dispatch.

**Sources à relire.**

- [src/mcp/cursiv/McpServer.cpp:67](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/mcp/cursiv/McpServer.cpp#L67) — Insertion sans recherche.
- [src/mcp/cursiv/McpServer.cpp:106](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/mcp/cursiv/McpServer.cpp#L106) — Toutes les entrées sont listées.
- [src/mcp/cursiv/McpServer.cpp:176](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/mcp/cursiv/McpServer.cpp#L176) — La première entrée correspondante est exécutée.
- [src/mcp/cursiv/McpServer.cpp:203](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/mcp/cursiv/McpServer.cpp#L203) — Même politique pour les ressources.

**Contexte MCP-002 — Négociation et catalogue de session.** createServer construit un catalogue avant les requêtes et le contrat interdit les enregistrements concurrents après le premier message. initialize choisit la version connue demandée ou la version locale la plus récente, annonce outils/ressources et instructions. Le catalogue n’est pas paginé et ne change pas pendant la session. handle ne maintient pas de phase MCP et n’impose donc pas lui-même la séquence initialize/initialized. Les noms dupliqués ne sont pas contrôlés.

**Limite de preuve de ce périmètre.** Analyse statique uniquement ; aucun nouvel outil MCP du projet exécuté pour cette fiche. Langium ne figure pas comme comparaison prévue pour ce périmètre.

<a id="finding-ws-mcp-format-document-uri"></a>

### WS-MCP-FORMAT-DOCUMENT-URI — Le formatter appelé par MCP reçoit une URI de document vide

**P2 · écart d’API · confiance forte.** Périmètres : MCP-019.

**Mécanisme identifié.** LspMcpService construit DocumentFormattingParams et n’en remplit que options. params.textDocument.uri garde sa valeur par défaut, même si document.uri contient bien l’URI demandée. Le même provider reçoit les paramètres complets dans le chemin LSP.

**Condition à vérifier.** Formatter personnalisé utilisant params.textDocument.uri pour une configuration par ressource, une délégation ou la validation du document demandé.

**Impact.** Configuration incorrecte, refus ou résultat différent entre MCP et LSP pour un même document. Les formatters d’exemple qui consultent seulement document et options peuvent masquer cette perte.

**Direction de correction.** Initialiser params.textDocument.uri à partir de document.uri avant l’appel et conserver une adaptation commune des paramètres requis lors des façades futures.

**Sources à relire.**

- [src/lsp/cursiv/mcp/LspMcpService.cpp:75](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/mcp/LspMcpService.cpp#L75) — Objet initialisé sans identifiant.
- [src/lsp/cursiv/mcp/LspMcpService.cpp:89](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/mcp/LspMcpService.cpp#L89) — Paramètres incomplets passés au provider.
- [src/lsp/cursiv/formatting/Formatter.hpp:25](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/formatting/Formatter.hpp#L25) — Signature publique DocumentFormattingParams.
- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:1208](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L1208) — Route de formatage LSP transmettant les paramètres d’origine.

**Contexte MCP-019 — Production du texte formaté.** format sélectionne la langue attachée au document via route_document_tool, refuse l’absence de Formatter et construit les options tabSize=2/insertSpaces=true avec remplacement par des valeurs valides reçues. Il appelle le formatter sous lecture, convertit les plages de LSP vers les types core, puis applique les edits à une copie textuelle pour rendre le document complet. Aucun setText ou update du workspace n’est effectué. La liste vide donne correctement le texte original. Le paramètre de document transmis au provider est toutefois laissé vide, ce qui casse une extension utilisant les paramètres complets de l’interface.

**Limite de preuve de ce périmètre.** Analyse statique uniquement ; aucun nouvel outil MCP, test ou reproducer exécuté. Les garanties des moteurs et du scoping sont examinées dans leurs propres périmètres. Une annulation/phase perdue avant l’exécution peut aussi être présentée comme absence de formatter par hasFormatter ; c’est une manifestation du défaut de routage MCP-004, et non une seconde absence d’implémentation.

<a id="finding-ws-mcp-outline-unnamed-containers"></a>

### WS-MCP-OUTLINE-UNNAMED-CONTAINERS — Le plan MCP omet les déclarations sous des conteneurs anonymes

**P2 · défaut · confiance forte.** Périmètres : MCP-010.

**Mécanisme identifié.** walk n’entre que dans entry.node pour les descriptions trouvées sous le conteneur courant. Une classe AST sans nom n’a pas d’entrée LocalSymbols dans son parent ; les déclarations qu’elle contient ne sont donc jamais atteintes. Le fallback racine ne répare pas ce saut.

**Condition à vérifier.** Grammaire avec un conteneur AST anonyme entre la racine/un symbole nommé et d’autres déclarations, par exemple Block non nommé contenant des variables.

**Impact.** Outline incomplet ou message 'this document declares no symbol' malgré des déclarations présentes ; divergence entre document-symbols MCP et DocumentSymbolProvider LSP.

**Direction de correction.** Parcourir l’AST et rattacher les nœuds nommés au plus proche ancêtre nommé, ou partager le collecteur NamedNodes LSP dans une couche core.

**Sources à relire.**

- [src/mcp/cursiv/DefaultMcpService.cpp:883](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/mcp/cursiv/DefaultMcpService.cpp#L883) — Seuls les symboles du conteneur sont parcourus.
- [src/mcp/cursiv/DefaultMcpService.cpp:900](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/mcp/cursiv/DefaultMcpService.cpp#L900) — Descente uniquement à partir d’une entrée nommée.
- [src/core/cursiv/references/DefaultScopeComputation.cpp:70](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/references/DefaultScopeComputation.cpp#L70) — Un nœud sans description n’ajoute pas de chemin de traversal.

**Contexte MCP-010 — Plan d’un document.** Le plan MCP reconstruit une hiérarchie depuis LocalSymbols, commence à Document puis essaie la racine si rien n’est trouvé. Les descendants ne sont visités qu’à travers une entrée de symbole existante ; cette stratégie ne traverse pas les conteneurs AST anonymes et diffère de collect_named_nodes du provider LSP. La profondeur supérieure à 16 est aussi coupée sans marqueur.

**Limite de preuve de ce périmètre.** Analyse statique uniquement ; aucun nouvel outil MCP du projet exécuté pour cette fiche. Langium ne figure pas comme comparaison prévue pour ce périmètre.

<a id="finding-ws-mcp-rename-quadratic-dedup"></a>

### WS-MCP-RENAME-QUADRATIC-DEDUP — La déduplication du renommage MCP effectue un nombre quadratique de comparaisons

**P2 · limite · confiance forte.** Périmètres : MCP-012.

**Mécanisme identifié.** Pour chaque occurrence, edit cherche (documentId,begin) dans un vector seen puis y ajoute la nouvelle clé. Avec N occurrences distinctes, ce seul parcours effectue N(N−1)/2 comparaisons, même si References a déjà trié chaque lot. Aucun polling d’annulation n’existe dans cette boucle de rendu/déduplication : le dernier lot matérialisé peut continuer après une annulation.

**Condition à vérifier.** Symbole avec beaucoup d’occurrences distinctes, notamment un lot unique de grande taille renvoyé par findReferences.

**Impact.** Coût superlinéaire et retard d’annulation propres à la façade MCP, en plus du coût légitime de collecte et de sortie de N edits. Aucune latence chiffrée ni saturation réelle n’est revendiquée.

**Direction de correction.** Employer un ensemble de clés avec égalité exacte, ou collecter puis trier/dédupliquer les spans comme le chemin LSP ; sonder le token lors du rendu par lots. Inclure la fin dans la clé si des providers personnalisés peuvent produire des spans différents au même début.

**Sources à relire.**

- [src/mcp/cursiv/DefaultMcpService.cpp:746](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/mcp/cursiv/DefaultMcpService.cpp#L746) — Vector des clés déjà vues.
- [src/mcp/cursiv/DefaultMcpService.cpp:749](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/mcp/cursiv/DefaultMcpService.cpp#L749) — Recherche linéaire avant chaque insertion.
- [src/mcp/cursiv/DefaultMcpService.cpp:752](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/mcp/cursiv/DefaultMcpService.cpp#L752) — Une nouvelle clé pour chaque occurrence distincte.
- [src/mcp/cursiv/DefaultMcpService.cpp:765](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/mcp/cursiv/DefaultMcpService.cpp#L765) — Rendu de tout le vector renvoyé sans contrôle de token.

**Contexte MCP-012 — Proposition de renommage.** La réponse est une proposition textuelle : elle ne modifie aucun document. Le nouveau nom est exigé comme chaîne, mais ni sa syntaxe ni les refus du RenameProvider LSP ne sont appliqués, conformément à la description explicite. Une référence non résolue conserve son propre span ; plusieurs cibles sont réunies et les débuts déjà rencontrés sont éliminés. Les positions et fins sont rendues dans chaque document source. La clé de déduplication omet la fin ; son exactitude suppose que des occurrences de même document et même début désignent le même nom. La barrière globale manquante est partagée avec MCP-011.

**Limite de preuve de ce périmètre.** Analyse statique uniquement ; aucun nouvel outil MCP, test ou reproducer exécuté. Les garanties des moteurs et du scoping sont examinées dans leurs propres périmètres.

<a id="finding-ws-mcp-request-id-range"></a>

### WS-MCP-REQUEST-ID-RANGE — Les IDs numériques MCP sont limités à int32 par la couche LSP

**P2 · limite · confiance forte.** Périmètres : MCP-001.

**Mécanisme identifié.** McpServer réutilise rpc::messageFromJson. Ce décodeur exige un entier numérique dans la plage lsp::json::Integer (int32), alors que l’interface expose un message JSON-RPC sans indiquer cette borne. Un client MCP utilisant des compteurs ou timestamps numériques plus grands reçoit InvalidRequest ; les IDs strings passent.

**Condition à vérifier.** Client MCP employant un identifiant entier supérieur à INT32_MAX ou inférieur à INT32_MIN.

**Impact.** Incompatibilité de transport sans rapport avec l’outil demandé ; les mêmes valeurs sous forme de chaînes fonctionnent.

**Direction de correction.** Documenter la restriction ou utiliser une représentation d’ID JSON-RPC adaptée à MCP et préserver sa valeur dans la réponse.

**Sources à relire.**

- [src/mcp/cursiv/McpServer.cpp:156](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/mcp/cursiv/McpServer.cpp#L156) — Utilisation du décodeur LSP.
- [deps/lsp-framework/lsp/jsonrpc/jsonrpc.cpp:25](https://github.com/ydaveluy/lsp-framework/blob/10d5d25721b22e06252f38c9fa93bf91dd702355/lsp/jsonrpc/jsonrpc.cpp#L25) — Contrôle de plage de Integer.
- [deps/lsp-framework/lsp/jsonrpc/jsonrpc.cpp:44](https://github.com/ydaveluy/lsp-framework/blob/10d5d25721b22e06252f38c9fa93bf91dd702355/lsp/jsonrpc/jsonrpc.cpp#L44) — Application aux IDs de requête.

**Contexte MCP-001 — Décodage et dispatch JSON-RPC.** Le dispatcher valide l’enveloppe via la dépendance JSON-RPC, retourne InvalidRequest pour les non-objets/batches et ignore notifications ou réponses entrantes. Les erreurs de syntaxe texte deviennent ParseError ; erreurs de paramètres/protocole et erreurs d’outil sont séparées. Les std::exception d’un outil deviennent isError avec texte, les ressources en erreur deviennent InternalError. La surface JSON-RPC héritée impose toutefois une borne int32 aux IDs numériques.

**Limite de preuve de ce périmètre.** Analyse statique uniquement ; aucun nouvel outil MCP du projet exécuté pour cette fiche. Langium ne figure pas comme comparaison prévue pour ce périmètre.

<a id="finding-ws-mcp-route-empty-success"></a>

### WS-MCP-ROUTE-EMPTY-SUCCESS — Une lecture MCP non exécutée devient un résultat vide réussi

**P2 · défaut · confiance forte.** Périmètres : MCP-004, MCP-007, MCP-011, MCP-012, MCP-013, MCP-014, MCP-015.

**Mécanisme identifié.** route_document_tool ignore le booléen du read final et n’enregistre pas si body a été exécuté. Un token annulé entre waitUntil et read, un document supprimé ou un état repassé sous le rung laisse out vide et isError=false. Les outils le réécrivent ensuite comme 'no symbol', 'nothing denoted' ou 'nothing may be written'. Le même mécanisme apparaît dans get-diagnostics : le retour du read est ignoré et une annulation avant exécution laisse out vide, transformé en « No diagnostics found. ».

**Condition à vérifier.** Annulation tardive ou écriture concurrente entre satisfaction de la phase et admission en lecture ; usage direct de McpServer/route_document_tool. Le tunnel LSP masque le cas token annulé par son contrôle final, pas toutes les courses d’état.

**Impact.** Résultat factuellement négatif alors que l’outil n’a pas pu examiner le document demandé.

**Direction de correction.** Propager une issue de routage distincte (annulé, supprimé, phase périmée) et réattendre/retry ou retourner isError, au lieu de confondre refus et collection vide.

**Sources à relire.**

- [src/mcp/cursiv/McpToolKit.cpp:109](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/mcp/cursiv/McpToolKit.cpp#L109) — Retour du read ignoré.
- [src/mcp/cursiv/McpToolKit.cpp:113](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/mcp/cursiv/McpToolKit.cpp#L113) — Sortie sans exécuter body.
- [src/mcp/cursiv/McpToolKit.cpp:118](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/mcp/cursiv/McpToolKit.cpp#L118) — Succès vide.
- [src/mcp/cursiv/DefaultMcpService.cpp:1008](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/mcp/cursiv/DefaultMcpService.cpp#L1008) — Transformation en conclusion métier négative.
- [src/mcp/cursiv/DefaultMcpService.cpp:396](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/mcp/cursiv/DefaultMcpService.cpp#L396) — Read dont le résultat est jeté.
- [src/mcp/cursiv/DefaultMcpService.cpp:417](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/mcp/cursiv/DefaultMcpService.cpp#L417) — Message sans diagnostic sur out vide.

**Contexte MCP-004 — Prérequis de lecture d’un document.** Le helper prend l’ID sous lecture courte puis attend le rung hors verrou. À l’admission finale, il relit le document par ID, vérifie son état et utilise bien document.language. Cette partie protège les données consultées, mais un refus de la dernière lecture ou un document remplacé/en cours de rebuild devient une réponse vide de succès, souvent transformée par les callers en conclusion métier négative.

**Limite de preuve de ce périmètre.** Analyse statique uniquement ; aucun nouvel outil MCP du projet exécuté pour cette fiche. Langium ne figure pas comme comparaison prévue pour ce périmètre.

**Contexte MCP-007 — Lecture des diagnostics.** get-diagnostics attend une phase workspace Validated, lit sous priorité normale et trie les documents par URI avant de rendre les erreurs de chaque document. Le filtre directory utilise l’index de dossiers et les positions affichées sont à base un. Le choix de n’afficher que les documents possédant des diagnostics est explicite. Le travail de spelling_actions est fait pour tous les diagnostics : sa complexité et l’absence de token propre sont déjà relevées en VAL-005. Le booléen de read ignoré présente le même risque de faux résultat vide que le helper de MCP-004 sur annulation tardive hors tunnel.

**Limite de preuve de ce périmètre.** Analyse statique uniquement ; aucun nouvel outil MCP du projet exécuté pour cette fiche. Langium ne figure pas comme comparaison prévue pour ce périmètre.

**Contexte MCP-011 — Références d’un nom.** Le handler distingue absence de nom, nom non résolu et ensemble de cibles. Il affiche le texte du span réellement dénoté, puis appelle References pour chaque cible avec includeDeclaration. Les offsets des occurrences sont reconvertis dans le document source, sous le verrou de lecture. Les lectures de références sont cependant globales alors que la barrière attend seulement le document interrogé. Les ensembles de surcharge peuvent aussi répéter des sites entre cibles : ce rendu n’effectue pas de déduplication globale, sans prétendre fournir un compteur d’occurrences uniques.

**Limite de preuve de ce périmètre.** Analyse statique uniquement ; aucun nouvel outil MCP, test ou reproducer exécuté. Les garanties des moteurs et du scoping sont examinées dans leurs propres périmètres.

**Contexte MCP-012 — Proposition de renommage.** La réponse est une proposition textuelle : elle ne modifie aucun document. Le nouveau nom est exigé comme chaîne, mais ni sa syntaxe ni les refus du RenameProvider LSP ne sont appliqués, conformément à la description explicite. Une référence non résolue conserve son propre span ; plusieurs cibles sont réunies et les débuts déjà rencontrés sont éliminés. Les positions et fins sont rendues dans chaque document source. La clé de déduplication omet la fin ; son exactitude suppose que des occurrences de même document et même début désignent le même nom. La barrière globale manquante est partagée avec MCP-011.

**Limite de preuve de ce périmètre.** Analyse statique uniquement ; aucun nouvel outil MCP, test ou reproducer exécuté. Les garanties des moteurs et du scoping sont examinées dans leurs propres périmètres.

**Contexte MCP-013 — Description d’un élément au curseur.** hover utilise le service References de la langue attachée au document source, conserve toutes les cibles de la déclaration et son span textuel, affiche leur tag de type AST et leur position de déclaration. Cette notion de type est celle de l’AST, pas un résultat universel d’inférence métier ; un langage peut remplacer l’outil pour cela. La documentation est demandée au document cible, ce qui est la bonne direction pour le multilangage, mais sa langue est reroutée par URI au lieu de lire home->language. Le cas est la même cause que celle déjà relevée dans symbols.

**Limite de preuve de ce périmètre.** Analyse statique uniquement ; aucun nouvel outil MCP, test ou reproducer exécuté. Les garanties des moteurs et du scoping sont examinées dans leurs propres périmètres.

**Contexte MCP-014 — Éléments attendus à une position.** features_at demande d’abord le remplacement du mot qui contient ou précède immédiatement le caret. Si ce remplacement n’offre rien et que le caret est à la fin du mot, il retente une insertion pure ; au milieu du mot il conserve la question de remplacement. expected-at indique « in place of » lorsqu’il rend les propositions de remplacement. Les graphies de terminaux sont développées, sinon le nom du token est donné, avec propriété et documentation grammaticale. Les lignes identiques sont supprimées sans supprimer des lignes qui nomment des propriétés différentes. Cette vue expose les attentes de la grammaire, pas les CompletionItem custom d’un langage. La gestion locale de la plage est cohérente ; seule l’issue de routage vide héritée reste à corriger.

**Limite de preuve de ce périmètre.** Analyse statique uniquement ; aucun nouvel outil MCP, test ou reproducer exécuté. Les garanties des moteurs et du scoping sont examinées dans leurs propres périmètres. Le coût et la finesse d’annulation de Parser::nextFeaturesAt ne sont pas conclus à partir de cette seule façade ; voir les fiches de complétion des moteurs.

**Contexte MCP-015 — Candidats d’une référence attendue.** L’outil cherche un owner CST, remonte pour trouver une référence de même nom et retente un choix unanime sur les types si aucun ancêtre ne la porte. Il transmet bien document, reference et narrow, puis borne la liste à 200 candidats avec marqueur d’élision. Cette reconstruction est une copie réduite du chemin LSP : elle perd la propriété textuelle, les informations type/entered du parser et la garde qui distingue une propriété ordinaire d’une référence. Enfin un break arrête la lecture après le premier site reconnu, même si NextFeature représente plusieurs possibilités admissibles.

**Limite de preuve de ce périmètre.** Analyse statique uniquement ; aucun nouvel outil MCP, test ou reproducer exécuté. Les garanties des moteurs et du scoping sont examinées dans leurs propres périmètres.

Anciennes clés regroupées : WS-MCP-DIAGNOSTICS-CANCEL-SUCCESS.

<a id="finding-ws-mcp-scope-context"></a>

### WS-MCP-SCOPE-CONTEXT — scope-candidates construit un contexte différent de celui de la complétion LSP

**P2 · défaut · confiance forte.** Périmètres : MCP-015.

**Mécanisme identifié.** Le ReferenceInfo MCP laisse property vide et ignore NextFeature.type/entered. Une ScopeProvider qui choisit une portée sur property ne reconnaît donc pas le site demandé. Avec des membres homonymes, la remontée peut sélectionner la référence d’un ancêtre alors que next.type identifie une autre classe ; un nœud représentant une règle nouvellement entrée n’est jamais construit. Le fallback global ne vérifie pas non plus qu’une propriété ordinaire du conteneur interdit de l’interpréter comme une référence trouvée sur un autre type. DefaultCompletionProvider possède ces contrôles.

**Condition à vérifier.** Provider de portée conditionné par la propriété ou le type du conteneur ; caret au début d’une nouvelle règle ; propriétés homonymes sur plusieurs types ; texte réparé dont le propriétaire est incomplet.

**Impact.** Candidats manquants, candidats d’un autre rôle ou absence annoncée de référence attendue. Le même langage peut compléter correctement dans LSP et mal guider un assistant via MCP.

**Direction de correction.** Extraire dans le core le constructeur de contexte à partir de NextFeature et du document, avec ownership du nœud provisoire ; réutiliser ses règles dans LSP et MCP, en conservant property, type, entered et narrow.

**Sources à relire.**

- [src/mcp/cursiv/DefaultMcpService.cpp:1043](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/mcp/cursiv/DefaultMcpService.cpp#L1043) — Choix par ancêtres sans NextFeature.type.
- [src/mcp/cursiv/DefaultMcpService.cpp:1048](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/mcp/cursiv/DefaultMcpService.cpp#L1048) — Fallback global sans garde de propriété ordinaire.
- [src/mcp/cursiv/DefaultMcpService.cpp:1063](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/mcp/cursiv/DefaultMcpService.cpp#L1063) — property non renseignée.
- [src/lsp/cursiv/completion/DefaultCompletionProvider.cpp:366](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/completion/DefaultCompletionProvider.cpp#L366) — Priorité au type propriétaire du site en LSP.
- [src/lsp/cursiv/completion/DefaultCompletionProvider.cpp:375](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/completion/DefaultCompletionProvider.cpp#L375) — Vérification des propriétés ordinaires.
- [src/lsp/cursiv/completion/DefaultCompletionProvider.cpp:396](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/completion/DefaultCompletionProvider.cpp#L396) — Création d’un conteneur provisoire.
- [src/lsp/cursiv/completion/DefaultCompletionProvider.cpp:409](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/completion/DefaultCompletionProvider.cpp#L409) — Propriété transmise au scope.

**Contexte MCP-015 — Candidats d’une référence attendue.** L’outil cherche un owner CST, remonte pour trouver une référence de même nom et retente un choix unanime sur les types si aucun ancêtre ne la porte. Il transmet bien document, reference et narrow, puis borne la liste à 200 candidats avec marqueur d’élision. Cette reconstruction est une copie réduite du chemin LSP : elle perd la propriété textuelle, les informations type/entered du parser et la garde qui distingue une propriété ordinaire d’une référence. Enfin un break arrête la lecture après le premier site reconnu, même si NextFeature représente plusieurs possibilités admissibles.

**Limite de preuve de ce périmètre.** Analyse statique uniquement ; aucun nouvel outil MCP, test ou reproducer exécuté. Les garanties des moteurs et du scoping sont examinées dans leurs propres périmètres.

<a id="finding-ws-mcp-scope-first-site"></a>

### WS-MCP-SCOPE-FIRST-SITE — scope-candidates ignore toutes les alternatives après le premier site reconnu

**P2 · défaut · confiance forte.** Périmètres : MCP-015.

**Mécanisme identifié.** Après visitScopeEntries, le break termine inconditionnellement la boucle des NextFeature. Ce point subsiste même si ReferenceInfo est correctement construit. Le contrat de NextFeature autorise plusieurs entrées pour les lectures qui remplissent des membres différents ; deux sites peuvent aussi porter des narrow différents. Seuls les candidats de la première lecture reconnue sont rendus, y compris lorsque cette portée est vide.

**Condition à vérifier.** Une position admet plusieurs propriétés de référence ou plusieurs sites de la même propriété avec restrictions différentes.

**Impact.** Liste de cibles dépendant de l’ordre des features et incomplète pour les autres continuations grammaticales admises.

**Direction de correction.** Examiner chaque contexte admissible, grouper les résultats par membre/type lorsque nécessaire et dédupliquer les candidats selon une identité explicite ; conserver une limite globale avec marqueur d’élision.

**Sources à relire.**

- [src/mcp/cursiv/DefaultMcpService.cpp:1036](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/mcp/cursiv/DefaultMcpService.cpp#L1036) — Plusieurs NextFeature sont disponibles.
- [src/mcp/cursiv/DefaultMcpService.cpp:1068](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/mcp/cursiv/DefaultMcpService.cpp#L1068) — Un en-tête est créé pour un site.
- [src/mcp/cursiv/DefaultMcpService.cpp:1086](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/mcp/cursiv/DefaultMcpService.cpp#L1086) — Arrêt inconditionnel après ce site.
- [src/core/cursiv/grammar/NextFeature.hpp:9](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/grammar/NextFeature.hpp#L9) — Une entrée par lecture et membre.
- [src/lsp/cursiv/completion/DefaultCompletionProvider.cpp:410](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/completion/DefaultCompletionProvider.cpp#L410) — Les restrictions de sites distincts doivent additionner leurs offres.

**Contexte MCP-015 — Candidats d’une référence attendue.** L’outil cherche un owner CST, remonte pour trouver une référence de même nom et retente un choix unanime sur les types si aucun ancêtre ne la porte. Il transmet bien document, reference et narrow, puis borne la liste à 200 candidats avec marqueur d’élision. Cette reconstruction est une copie réduite du chemin LSP : elle perd la propriété textuelle, les informations type/entered du parser et la garde qui distingue une propriété ordinaire d’une référence. Enfin un break arrête la lecture après le premier site reconnu, même si NextFeature représente plusieurs possibilités admissibles.

**Limite de preuve de ce périmètre.** Analyse statique uniquement ; aucun nouvel outil MCP, test ou reproducer exécuté. Les garanties des moteurs et du scoping sont examinées dans leurs propres périmètres.

<a id="finding-ws-mcp-scratch-identity-retention"></a>

### WS-MCP-SCRATCH-IDENTITY-RETENTION — Des noms de fichiers successifs font croître indéfiniment les identités temporaires

**P2 · limite · confiance forte.** Périmètres : MCP-005.

**Mécanisme identifié.** La réutilisation d’un numéro de slot ne réutilise une URI que si input.name est identique. Chaque nouveau basename donné à validate produit une nouvelle URI et DefaultDocuments conserve pour toujours sa ligne, son URI et son index de dossiers après suppression. La borne n’est donc pas le maximum d’appels simultanés indiqué par le commentaire.

**Condition à vérifier.** Longue session effectuant des validations séquentielles avec des noms de fichier variés, même avec un seul slot actif.

**Impact.** Croissance permanente des métadonnées Documents et coût croissant d’all()/réservations, alors que le texte temporaire est supprimé.

**Direction de correction.** Employer une identité stable par slot/langue, séparer le nom logique de l’URI de stockage, ou ajouter un stockage éphémère dont les identités sont récupérables.

**Sources à relire.**

- [src/mcp/cursiv/DefaultMcpService.cpp:213](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/mcp/cursiv/DefaultMcpService.cpp#L213) — Nom fourni conservé.
- [src/mcp/cursiv/DefaultMcpService.cpp:229](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/mcp/cursiv/DefaultMcpService.cpp#L229) — Nom dans l’URI.
- [src/core/cursiv/workspace/DefaultDocuments.cpp:36](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultDocuments.cpp#L36) — Nouvelle ligne par URI.
- [src/core/cursiv/workspace/DefaultDocuments.cpp:97](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultDocuments.cpp#L97) — Ligne conservée après suppression.

**Contexte MCP-005 — Identités et réservations temporaires.** ScratchSlots attribue des numéros exclusifs sous mutex et ScratchLease les rend sur toute sortie du handler. Le nom conserve seulement le basename et le languageId peut fixer la langue même sans extension. Cependant, l’URI inclut ce nom variable et la restitution du slot peut allouer dans un destructeur implicitement noexcept ; les bornes de durée de vie promises ne suffisent donc pas à borner les identités ni à garantir une libération sans échec.

**Limite de preuve de ce périmètre.** Analyse statique uniquement ; aucun nouvel outil MCP du projet exécuté pour cette fiche. Langium ne figure pas comme comparaison prévue pour ce périmètre.

<a id="finding-ws-mcp-scratch-release-allocation"></a>

### WS-MCP-SCRATCH-RELEASE-ALLOCATION — La restitution d’un slot peut terminer le processus en cas d’allocation échouée

**P2 · défaut · confiance forte.** Périmètres : MCP-005.

**Mécanisme identifié.** ScratchLease::~ScratchLease appelle ScratchSlots::release, qui fait _free.push_back sous mutex. Le vecteur peut allouer, notamment lors de la première restitution ; bad_alloc traverse un destructeur implicitement noexcept et provoque std::terminate au lieu d’une erreur MCP.

**Condition à vérifier.** Échec d’allocation au retour d’un slot, y compris pendant le déroulement d’une autre exception.

**Impact.** Arrêt du serveur LSP/MCP entier à la fin d’une validation.

**Direction de correction.** Réserver la capacité nécessaire lors de l’acquisition, ou utiliser une structure de restitution sans allocation et un destructeur explicitement noexcept.

**Sources à relire.**

- [src/mcp/cursiv/DefaultMcpService.cpp:159](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/mcp/cursiv/DefaultMcpService.cpp#L159) — push_back potentiellement allouant.
- [src/mcp/cursiv/DefaultMcpService.cpp:180](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/mcp/cursiv/DefaultMcpService.cpp#L180) — Appel dans le destructeur.

**Contexte MCP-005 — Identités et réservations temporaires.** ScratchSlots attribue des numéros exclusifs sous mutex et ScratchLease les rend sur toute sortie du handler. Le nom conserve seulement le basename et le languageId peut fixer la langue même sans extension. Cependant, l’URI inclut ce nom variable et la restitution du slot peut allouer dans un destructeur implicitement noexcept ; les bornes de durée de vie promises ne suffisent donc pas à borner les identités ni à garantir une libération sans échec.

**Limite de preuve de ce périmètre.** Analyse statique uniquement ; aucun nouvel outil MCP du projet exécuté pour cette fiche. Langium ne figure pas comme comparaison prévue pour ce périmètre.

<a id="finding-ws-mcp-stdio-disk-snapshot"></a>

### WS-MCP-STDIO-DISK-SNAPSHOT — Le serveur MCP stdio ne renouvelle pas les fichiers après son démarrage

**P2 · limite · confiance forte.** Périmètres : MCP-017.

**Mécanisme identifié.** run_mcp effectue initializeWorkspace une fois puis ne fait que handleText sur chaque ligne. Le catalogue standard ne contient aucun outil de refresh/update ; text-document-content copie le document déjà détenu et validate ne met à jour que son scratch. Un fichier modifié, créé ou supprimé sur disque après le démarrage n’est donc pas reflété par cette session standard.

**Condition à vérifier.** Assistant utilisant une session stdio durable puis enregistrant des modifications avec ses outils de fichiers ; application conservant les services par défaut, sans watcher ni outil personnalisé.

**Impact.** Lecture, symboles, références et diagnostics reposent sur la photographie initiale ; validate lie son texte temporaire contre des documents voisins potentiellement anciens. Un redémarrage est nécessaire pour retrouver l’état courant du disque.

**Direction de correction.** Ajouter une opération explicite de resynchronisation ou un watcher qui transmet les changements à DocumentBuilder ; à défaut, documenter clairement le mode photographie et le redémarrage requis. Préserver les buffers ouverts dans les autres modes.

**Sources à relire.**

- [src/lsp/cursiv/runtime/LanguageServerMain.cpp:125](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/LanguageServerMain.cpp#L125) — Seul scan initial.
- [src/lsp/cursiv/runtime/LanguageServerMain.cpp:128](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/LanguageServerMain.cpp#L128) — Boucle qui ne surveille pas les fichiers.
- [src/mcp/cursiv/DefaultMcpService.cpp:252](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/mcp/cursiv/DefaultMcpService.cpp#L252) — Catalogue standard sans rechargement.
- [src/mcp/cursiv/DefaultMcpService.cpp:845](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/mcp/cursiv/DefaultMcpService.cpp#L845) — Lecture du texte déjà détenu.
- [src/mcp/cursiv/DefaultMcpService.cpp:477](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/mcp/cursiv/DefaultMcpService.cpp#L477) — update de validate porte seulement sur le scratch.
- [docs/reference/mcp.md:13](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/docs/reference/mcp.md#L13) — Mode documenté comme lecture des fichiers sauvegardés.

**Contexte MCP-017 — Session MCP sur stdio.** Le mode stdio crée une session depuis le dossier absolu, initialise le workspace avant de lire les messages et garde le McpServer vivant pour toute la boucle. getline délimite un message JSON par ligne, les lignes vides sont ignorées, les notifications ne produisent pas de réponse et chaque réponse est terminée/flushée. Le traitement est séquentiel : aucun parallélisme ni annulation MCP entrante ne sont obtenus pendant un appel. Les retours EOF ferment normalement la boucle. Le principal manque fonctionnel est le renouvellement des fichiers : après l’initialisation aucun événement disque ni outil de rechargement n’alimente ce mode.

**Limite de preuve de ce périmètre.** Analyse statique uniquement ; aucun nouvel outil MCP, test ou reproducer exécuté. Les garanties des moteurs et du scoping sont examinées dans leurs propres périmètres. Le manque est classé comme limite de mode, pas comme violation d’une promesse explicite de surveillance. Les transports LSP alimentés par didChange/didChangeWatchedFiles ont un autre contrat.

<a id="finding-ws-mcp-symbols-missing-cst"></a>

### WS-MCP-SYMBOLS-MISSING-CST — symbols indexe le CST sans vérifier le fallback symbolId

**P2 · écart d’API · confiance forte.** Périmètres : MCP-009.

**Mécanisme identifié.** Si le NameProvider ne fournit pas de nameNode, l’outil utilise description.symbolId puis accède directement au CST. DefaultAstNodeDescriptionProvider peut produire kNoNode pour un nœud construit sans CST, et IndexManager conserve les descriptions sans filtrer valid(). Le provider LSP de workspace symbols ignore au contraire les descriptions sans plage de nom.

**Condition à vérifier.** Langue exportant un nœud synthétique/hand-built avec documentId valide, mais sans nameNode ni cstNodeId.

**Impact.** Assertion en debug ; accès hors limites du CST en release pendant tools/call symbols.

**Direction de correction.** Vérifier symbolId != kNoNode et symbolId<cst.size(), puis rendre le nom sans position ou omettre la description.

**Sources à relire.**

- [src/mcp/cursiv/DefaultMcpService.cpp:564](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/mcp/cursiv/DefaultMcpService.cpp#L564) — Fallback sans vérification.
- [src/mcp/cursiv/DefaultMcpService.cpp:567](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/mcp/cursiv/DefaultMcpService.cpp#L567) — Accès direct au CST.
- [src/core/cursiv/workspace/DefaultAstNodeDescriptionProvider.cpp:27](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultAstNodeDescriptionProvider.cpp#L27) — Description héritant du cstNodeId du nœud.
- [src/core/cursiv/workspace/DefaultIndexManager.cpp:122](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultIndexManager.cpp#L122) — Exports conservés tels quels.
- [src/core/cursiv/syntax-tree/CstRun.hpp:137](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/syntax-tree/CstRun.hpp#L137) — Accès protégé uniquement par assert.

**Contexte MCP-009 — Recherche de symboles exportés.** symbols filtre par sous-chaîne avec casse ASCII, compte les correspondances et interrompt réellement le visiteur après 200 ; la réponse annonce alors les résultats élidés et suggère de préciser la requête. Les types et positions sont rendus par export. Le calcul de langue est divergent du document lié ; le fallback de plage suppose également un symbolId valide et ne protège pas les descriptions synthétiques sans CST.

**Limite de preuve de ce périmètre.** Analyse statique uniquement ; aucun nouvel outil MCP du projet exécuté pour cette fiche. Langium ne figure pas comme comparaison prévue pour ce périmètre.

<a id="finding-ws-mcp-uri-nonhierarchical"></a>

### WS-MCP-URI-NONHIERARCHICAL — Les URI sans // sont transformées en chemins de fichier

**P2 · défaut · confiance forte.** Périmètres : MCP-003.

**Mécanisme identifié.** tool_file_uri ne reconnaît une URI que si la chaîne contient ://. Des identifiants comme untitled:Note ou file:/tmp/a sont passés à path_to_file_uri et deviennent un autre identifiant absolu. Le contrat public indique pourtant que toute URI existante passe telle quelle.

**Condition à vérifier.** Document virtuel/éditeur déjà présent sous une URI sans autorité, puis cette URI est recopiée depuis documents dans un outil positionnel ou text-document-content.

**Impact.** Document déclaré absent et outils incapables de lire/naviguer dans un buffer pourtant détenu ; incohérence entre découverte et utilisation.

**Direction de correction.** Détecter le schéma URI selon sa grammaire, en distinguant les lettres de lecteur Windows, puis normaliser par les helpers d’URI existants.

**Sources à relire.**

- [src/mcp/cursiv/McpToolKit.cpp:61](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/mcp/cursiv/McpToolKit.cpp#L61) — Test limité à ://.
- [src/mcp/cursiv/McpToolKit.hpp:37](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/mcp/cursiv/McpToolKit.hpp#L37) — Contrat pour toute URI.
- [src/mcp/cursiv/DefaultMcpService.cpp:836](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/mcp/cursiv/DefaultMcpService.cpp#L836) — Usage pour lire le texte courant.

**Contexte MCP-003 — Schémas, URI et positions.** Les helpers rendent explicites les positions à base un en unités UTF-16, contrôlent file/string et les entiers positifs, puis soustraient un avant conversion interne. Les schémas annoncent les mêmes champs requis ; minimum n’est pas codé dans JSON Schema, mais le texte et la validation l’imposent. Le helper d’URI, en revanche, assimile présence de :// et validité d’un schéma, ce qui exclut des URI légitimes retournées par documents.

**Limite de preuve de ce périmètre.** Analyse statique uniquement ; aucun nouvel outil MCP du projet exécuté pour cette fiche. Langium ne figure pas comme comparaison prévue pour ce périmètre.

<a id="finding-ws-mcp-validate-categories"></a>

### WS-MCP-VALIDATE-CATEGORIES — validate omet les catégories lentes tout en annonçant le texte valide

**P2 · défaut · confiance forte.** Périmètres : MCP-006.

**Mécanisme identifié.** Le scratch est construit par DocumentBuilder::update, dont les options par défaut demandent seulement built_in et fast. Le handler n’effectue ensuite aucun build demandant toutes les catégories et expose aucun paramètre de choix. Attendre Validated ne comble pas ce manque : le contrat du builder définit cet état relativement aux catégories déjà demandées. Une erreur d’un check slow ou d’une autre catégorie enregistrée reste absente du résultat « No diagnostics: the text is valid. ».

**Condition à vérifier.** Langue ayant enregistré un check slow ou une autre catégorie non incluse dans les options d’update par défaut, et texte dont le seul défaut relève de ce check.

**Impact.** Faux sentiment de validation complète pour le consommateur MCP ; impossibilité de demander ce check par les arguments de l’outil standard.

**Direction de correction.** Définir le niveau de validation de l’outil explicitement : demander toutes les catégories pour la validation finale, ou exposer le choix et nommer les catégories exécutées dans le verdict. Éviter de modifier globalement updateBuildOptions pour une seule requête concurrente.

**Sources à relire.**

- [src/mcp/cursiv/DefaultMcpService.cpp:477](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/mcp/cursiv/DefaultMcpService.cpp#L477) — Le handler ne demande qu’un update.
- [src/core/cursiv/workspace/DefaultDocumentBuilder.cpp:49](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultDocumentBuilder.cpp#L49) — Défaut built_in/fast.
- [src/core/cursiv/workspace/DefaultDocumentBuilder.cpp:522](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultDocumentBuilder.cpp#L522) — Ces options sont appliquées aux documents demandés.
- [src/core/cursiv/workspace/DocumentBuilder.hpp:85](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DocumentBuilder.hpp#L85) — Validated ne signifie pas que toutes les catégories ont été exécutées.
- [src/mcp/cursiv/DefaultMcpService.cpp:497](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/mcp/cursiv/DefaultMcpService.cpp#L497) — Conclusion absolue de validité.

**Contexte MCP-006 — Validation d’un texte éphémère.** validate installe un document temporaire puis le retire via deux écritures Durable ; le flag installed évite de supprimer une URI dont l’installation a échoué. L’attente de Validated est séparée de la lecture, mais la lecture finale ne revalide pas la phase et son refus n’est pas distingué d’un bloc sans diagnostics. De plus, le document temporaire participe aux mêmes exports et mises à jour que le workspace qu’il observe. L’update utilise aussi les catégories built_in/fast par défaut : Validated signifie uniquement que ces catégories demandées ont été exécutées, alors que la réponse conclut sans réserve sur la validité du texte.

**Limite de preuve de ce périmètre.** Analyse statique uniquement ; aucun nouvel outil MCP du projet exécuté pour cette fiche. Langium ne figure pas comme comparaison prévue pour ce périmètre.

<a id="finding-ws-mcp-validate-cleanup-failure"></a>

### WS-MCP-VALIDATE-CLEANUP-FAILURE — Un échec du nettoyage peut laisser un scratch détenu après libération du slot

**P2 · défaut · confiance forte.** Périmètres : MCP-006.

**Mécanisme identifié.** discard repose sur DocumentBuilder::update et peut lever avant que cleanUpDeleted ait retiré le document. Le catch rappelle discard mais ne conserve pas la réservation si cette tentative lève ; ScratchLease rend le slot alors que l’URI peut rester détenue. Un appel ultérieur avec le même nom/slot échoue à addDocument.

**Condition à vérifier.** Exception de callback/allocation dans l’update de suppression avant retrait effectif du scratch.

**Impact.** Document temporaire et exports persistants, slot réutilisé pour une URI occupée et erreur initiale potentiellement masquée.

**Direction de correction.** Rendre le retrait du scratch robuste et observable ; ne rendre la réservation qu’après confirmation du retrait, avec nettoyage différé en cas d’échec.

**Sources à relire.**

- [src/mcp/cursiv/DefaultMcpService.cpp:455](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/mcp/cursiv/DefaultMcpService.cpp#L455) — Nettoyage sans issue de secours.
- [src/mcp/cursiv/DefaultMcpService.cpp:499](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/mcp/cursiv/DefaultMcpService.cpp#L499) — Nouvelle tentative dans catch puis libération implicite.
- [src/core/cursiv/workspace/DefaultDocumentBuilder.cpp:481](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultDocumentBuilder.cpp#L481) — Étapes potentiellement levantes avant cleanUpDeleted.
- [src/core/cursiv/workspace/DefaultDocuments.cpp:55](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultDocuments.cpp#L55) — Refus d’une URI encore occupée.

**Contexte MCP-006 — Validation d’un texte éphémère.** validate installe un document temporaire puis le retire via deux écritures Durable ; le flag installed évite de supprimer une URI dont l’installation a échoué. L’attente de Validated est séparée de la lecture, mais la lecture finale ne revalide pas la phase et son refus n’est pas distingué d’un bloc sans diagnostics. De plus, le document temporaire participe aux mêmes exports et mises à jour que le workspace qu’il observe. L’update utilise aussi les catégories built_in/fast par défaut : Validated signifie uniquement que ces catégories demandées ont été exécutées, alors que la réponse conclut sans réserve sur la validité du texte.

**Limite de preuve de ce périmètre.** Analyse statique uniquement ; aucun nouvel outil MCP du projet exécuté pour cette fiche. Langium ne figure pas comme comparaison prévue pour ce périmètre.

<a id="finding-ws-mcp-validate-read-admission"></a>

### WS-MCP-VALIDATE-READ-ADMISSION — validate peut annoncer un texte valide sans avoir lu un état Validated

**P2 · défaut · confiance forte.** Périmètres : MCP-006.

**Mécanisme identifié.** Après waitUntil(Validated), le callback de read vérifie uniquement que le document existe ; il ne vérifie ni la réussite du read ni document.state. Une annulation tardive ou un rebuild concurrent qui a vidé les diagnostics produit out vide puis le message 'No diagnostics: the text is valid.'.

**Condition à vérifier.** Entre l’attente et la lecture, le token est arrêté ou une autre validation/update réinitialise le document temporaire, par exemple lors d’un nouveau symbole réparant ses liens.

**Impact.** Fausse conclusion de validité ; le tunnel LSP protège seulement le cas où son token final reste annulé.

**Direction de correction.** Exiger un read exécuté et state>=Validated dans la lecture stable ; retourner une issue d’échec ou recommencer l’attente si le snapshot a changé.

**Sources à relire.**

- [src/mcp/cursiv/DefaultMcpService.cpp:484](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/mcp/cursiv/DefaultMcpService.cpp#L484) — Attente terminée avant read.
- [src/mcp/cursiv/DefaultMcpService.cpp:494](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/mcp/cursiv/DefaultMcpService.cpp#L494) — Absence de recontrôle de phase.
- [src/mcp/cursiv/DefaultMcpService.cpp:497](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/mcp/cursiv/DefaultMcpService.cpp#L497) — Succès dérivé uniquement de chaîne vide.

**Contexte MCP-006 — Validation d’un texte éphémère.** validate installe un document temporaire puis le retire via deux écritures Durable ; le flag installed évite de supprimer une URI dont l’installation a échoué. L’attente de Validated est séparée de la lecture, mais la lecture finale ne revalide pas la phase et son refus n’est pas distingué d’un bloc sans diagnostics. De plus, le document temporaire participe aux mêmes exports et mises à jour que le workspace qu’il observe. L’update utilise aussi les catégories built_in/fast par défaut : Validated signifie uniquement que ces catégories demandées ont été exécutées, alors que la réponse conclut sans réserve sur la validité du texte.

**Limite de preuve de ce périmètre.** Analyse statique uniquement ; aucun nouvel outil MCP du projet exécuté pour cette fiche. Langium ne figure pas comme comparaison prévue pour ce périmètre.

<a id="finding-ws-mcp-validate-shared-exports"></a>

### WS-MCP-VALIDATE-SHARED-EXPORTS — Les validations temporaires ne sont pas isolées entre appels

**P2 · limite · confiance forte.** Périmètres : MCP-006.

**Mécanisme identifié.** Le scratch est ajouté à Documents puis passé à DocumentBuilder::update, qui l’indexe comme un autre document. Avant sa suppression, ses exports et diagnostics sont visibles dans le workspace et peuvent affecter les liens d’autres appels à validate. Les slots isolent les URI, pas les symboles ni les résultats.

**Condition à vérifier.** Appels MCP concurrents de validate ou lecture d’outils pendant un validate ; textes temporaires définissant des noms globaux.

**Impact.** Résultats dépendant de la concurrence et exposition transitoire de symboles qui ne seront jamais sauvegardés.

**Direction de correction.** Valider dans un contexte isolé lisant l’index stable du workspace sans publier ses exports, ou sérialiser/documenter explicitement cette visibilité temporaire.

**Sources à relire.**

- [src/mcp/cursiv/DefaultMcpService.cpp:475](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/mcp/cursiv/DefaultMcpService.cpp#L475) — Ajout au store partagé.
- [src/mcp/cursiv/DefaultMcpService.cpp:477](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/mcp/cursiv/DefaultMcpService.cpp#L477) — Build/exports partagés.
- [src/mcp/cursiv/DefaultMcpService.cpp:503](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/mcp/cursiv/DefaultMcpService.cpp#L503) — Suppression seulement après le résultat.

**Contexte MCP-006 — Validation d’un texte éphémère.** validate installe un document temporaire puis le retire via deux écritures Durable ; le flag installed évite de supprimer une URI dont l’installation a échoué. L’attente de Validated est séparée de la lecture, mais la lecture finale ne revalide pas la phase et son refus n’est pas distingué d’un bloc sans diagnostics. De plus, le document temporaire participe aux mêmes exports et mises à jour que le workspace qu’il observe. L’update utilise aussi les catégories built_in/fast par défaut : Validated signifie uniquement que ces catégories demandées ont été exécutées, alors que la réponse conclut sans réserve sur la validité du texte.

**Limite de preuve de ce périmètre.** Analyse statique uniquement ; aucun nouvel outil MCP du projet exécuté pour cette fiche. Langium ne figure pas comme comparaison prévue pour ce périmètre.

<a id="finding-ws-mcp-workspace-reference-barrier"></a>

### WS-MCP-WORKSPACE-REFERENCE-BARRIER — Les références et le renommage MCP peuvent lire un index global encore incomplet

**P2 · défaut · confiance forte.** Périmètres : MCP-011, MCP-012.

**Mécanisme identifié.** references et rename appellent route_document_tool avec IndexedReferences pour l’ID du seul document au curseur. Le helper attend ce document puis entre en lecture Immediate. Or le builder publie chaque état documentaire au fil du travail, avant la barrière de fin de phase workspace, et settle permet déjà ces lectures. Un document A terminé peut ainsi interroger l’index alors que le document B qui le référence attend encore updateReferences. Les routes LSP correspondantes exigent WorkspaceState::IndexedReferences.

**Condition à vérifier.** Un build en cours laisse A à IndexedReferences pendant que B n’a pas encore indexé ses références vers A ; appel direct MCP ou requête de tunnel qui chevauche une écriture plus tardive/concurrente. La queue du tunnel attend les notifications antérieures, donc ce constat ne signifie pas que le simple ordre didChange puis requête LSP contourne cette queue.

**Impact.** Occurrences omises et proposition de renommage incomplète, présentées comme couvrant tout le workspace. Il s’agit d’un défaut de cohérence de phase, pas d’une assertion de data race dans IndexManager.

**Direction de correction.** Ajouter une exigence de phase workspace au routage MCP et l’attendre hors du verrou, puis admettre une lecture qui revalide cette exigence ; partager le contrat de routage avec LSP.

**Sources à relire.**

- [src/mcp/cursiv/DefaultMcpService.cpp:595](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/mcp/cursiv/DefaultMcpService.cpp#L595) — Barrière de references.
- [src/mcp/cursiv/DefaultMcpService.cpp:729](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/mcp/cursiv/DefaultMcpService.cpp#L729) — Même barrière pour rename.
- [src/mcp/cursiv/McpToolKit.cpp:105](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/mcp/cursiv/McpToolKit.cpp#L105) — Attente limitée à un ID.
- [src/mcp/cursiv/McpToolKit.cpp:117](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/mcp/cursiv/McpToolKit.cpp#L117) — Lecture Immediate.
- [src/core/cursiv/workspace/DefaultDocumentBuilder.cpp:95](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultDocumentBuilder.cpp#L95) — Publication documentaire dès la fin de l’étape locale.
- [src/core/cursiv/workspace/DefaultDocumentBuilder.cpp:138](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultDocumentBuilder.cpp#L138) — Publication globale seulement après le travail de phase.
- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:1001](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L1001) — Barrière workspace de references LSP.
- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:1029](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L1029) — Barrière workspace de rename LSP.

**Contexte MCP-011 — Références d’un nom.** Le handler distingue absence de nom, nom non résolu et ensemble de cibles. Il affiche le texte du span réellement dénoté, puis appelle References pour chaque cible avec includeDeclaration. Les offsets des occurrences sont reconvertis dans le document source, sous le verrou de lecture. Les lectures de références sont cependant globales alors que la barrière attend seulement le document interrogé. Les ensembles de surcharge peuvent aussi répéter des sites entre cibles : ce rendu n’effectue pas de déduplication globale, sans prétendre fournir un compteur d’occurrences uniques.

**Limite de preuve de ce périmètre.** Analyse statique uniquement ; aucun nouvel outil MCP, test ou reproducer exécuté. Les garanties des moteurs et du scoping sont examinées dans leurs propres périmètres.

**Contexte MCP-012 — Proposition de renommage.** La réponse est une proposition textuelle : elle ne modifie aucun document. Le nouveau nom est exigé comme chaîne, mais ni sa syntaxe ni les refus du RenameProvider LSP ne sont appliqués, conformément à la description explicite. Une référence non résolue conserve son propre span ; plusieurs cibles sont réunies et les débuts déjà rencontrés sont éliminés. Les positions et fins sont rendues dans chaque document source. La clé de déduplication omet la fin ; son exactitude suppose que des occurrences de même document et même début désignent le même nom. La barrière globale manquante est partagée avec MCP-011.

**Limite de preuve de ce périmètre.** Analyse statique uniquement ; aucun nouvel outil MCP, test ou reproducer exécuté. Les garanties des moteurs et du scoping sont examinées dans leurs propres périmètres.

<a id="finding-ws-ref-casefold-ascii"></a>

### WS-REF-CASEFOLD-ASCII — Le scope insensible à la casse ne rapproche pas les lettres non ASCII

**P2 · limite · confiance forte.** Périmètres : REF-005, TXT-002.

**Mécanisme identifié.** isCaseSensitive=false réutilise folded_hash/folded_equal, qui ne abaissent que A..Z et laissent tous les octets non ASCII inchangés. Des noms Unicode dont seule la casse diffère restent distincts, par exemple École et école.

**Condition à vérifier.** Un langage autorise des identifiants Unicode et configure son scope comme insensible à la casse.

**Impact.** Résolution différente de Langium et de l’attente Unicode ; les noms ASCII suivent la politique annoncée.

**Direction de correction.** Documenter explicitement la portée ASCII ou introduire une normalisation Unicode commune aux clés, comparaisons et dépendances ImportedNames.

**Sources à relire.**

- [src/core/cursiv/utils/CaseFold.hpp:16](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/utils/CaseFold.hpp#L16) — Octets non ASCII inchangés.
- [src/core/cursiv/workspace/LocalSymbols.hpp:71](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/LocalSymbols.hpp#L71) — Hash commun des scopes locaux.
- [src/core/cursiv/workspace/DefaultIndexManager.cpp:302](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultIndexManager.cpp#L302) — Comparaison globale avec folded_equal.
- [langium/packages/langium/src/references/scope.ts:80](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/references/scope.ts#L80) — Langium utilise String.toLowerCase().

**Contexte REF-005 — Résolution d’un nom dans les scopes.** Le lookup grimpe du conteneur le plus proche vers la racine, filtre le type accepté et le narrowing avant de retenir un homonyme, puis interroge le global. La politique de casse est résolue une fois et appliquée aux deux niveaux. Un lookup qui atteint le global enregistre le hash même en cas d’échec. Le choix local/global et l’ordre des homonymes sont cohérents ; la portée ASCII du pliage est un écart fonctionnel avec Langium.

**Limite de preuve de ce périmètre.** Lecture statique du code, des interfaces et des callers ; ni exécution ni scénario reproduit.

Comparaison incluse : [REF-005](#comparison-ref-005).

**Contexte TXT-002 — Case folding et classes Unicode.** Le hash et l’égalité de noms appliquent exactement le même pliage ASCII, avec longueur dans le hash et comparaison exacte des mots pliés. Les octets non ASCII restent inchangés. lower_case constitue une autre opération : tables scalaires Unicode, expansion U+0130, sigma final tenant compte du contexte, conservation des octets invalides. Cette séparation est documentée : remplacer le hash seul par un lower Unicode briserait sa cohérence avec l’égalité.

**Limite de preuve de ce périmètre.** L’exactitude exhaustive des tables Unicode et leur version relève de TXT-003.

<a id="finding-ws-ref-description-node-required"></a>

### WS-REF-DESCRIPTION-NODE-REQUIRED — Une description identifiée mais sans pointeur AST ne peut pas être liée par le service par défaut

**P2 · écart d’API · confiance forte.** Périmètres : REF-007.

**Mécanisme identifié.** DefaultDocumentReferences::resolve rejette toute description dont node est nul ; resolveAll écarte ces entrées. Aucun chargement via documentId/symbolId n’est tenté, alors que la structure présente le pointeur comme raccourci et les IDs comme identité.

**Condition à vérifier.** Index personnalisé, bibliothèque pré-indexée ou adaptation d’un fournisseur Langium renvoyant des descriptions sans nœud chargé.

**Impact.** La cible visible dans l’index reste introuvable pour les références ; il faut matérialiser et conserver les AST ou remplacer davantage de services.

**Direction de correction.** Préciser que node est obligatoire pour le linking par défaut ou fournir un hook de chargement par identité avec un contrat de durée de vie.

**Sources à relire.**

- [src/core/cursiv/syntax-tree/DefaultDocumentReferences.cpp:73](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/syntax-tree/DefaultDocumentReferences.cpp#L73) — Retour en échec si node==nullptr.
- [src/core/cursiv/syntax-tree/DefaultDocumentReferences.cpp:173](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/syntax-tree/DefaultDocumentReferences.cpp#L173) — Filtre équivalent sur MultiRef.
- [src/core/cursiv/workspace/AstNodeDescription.hpp:12](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/AstNodeDescription.hpp#L12) — Identité et pointeur présenté comme raccourci.
- [langium/packages/langium/src/references/linker.ts:384](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/references/linker.ts#L384) — loadAstNode récupère le document et le chemin si node absent.

**Contexte REF-007 — Conversion des candidats en résultat de linking.** Le linker transforme un nom vide ou absent en erreur, retient le premier candidat simple et collecte tous les candidats multiples. La déduplication commence par un petit tableau puis bascule vers un set avec la même clé document/symbole/nœud ; deux AST créés sur un même CST restent distincts ici. Les résultats peuvent porter un LinkingError, mais leur consommation conserve seulement un état et exige une cible matérialisée.

**Limite de preuve de ce périmètre.** Lecture statique du code, des interfaces et des callers ; ni exécution ni scénario reproduit.

Comparaison incluse : [REF-007](#comparison-ref-007).

<a id="finding-ws-ref-error-cause-lost"></a>

### WS-REF-ERROR-CAUSE-LOST — Le linking perd les causes d’erreur personnalisées et les exceptions des providers

**P2 · écart d’API · confiance forte.** Périmètres : REF-009.

**Mécanisme identifié.** contained convertit toute exception non liée à l’annulation en valeur vide. Les LinkingError retournés par getCandidate/getCandidates sont également réduits à ErrorNotFound sans conserver message ni description. La validation recrée ensuite un message avec createLinkingError(site, why). Un détail d’erreur calculé au moment de la résolution est donc perdu, sauf si le hook sait le recalculer.

**Condition à vérifier.** Un Linker personnalisé retourne une erreur contextuelle, ou un ScopeProvider/Linker lève une exception métier/technique.

**Impact.** Le diagnostic peut annoncer une simple absence de nom à la place d’une cause utile ; aucun journal de cette exception n’est émis à cet endroit. Le débogage des extensions devient plus difficile.

**Direction de correction.** Conserver un payload d’erreur parcimonieux pour les seules références en échec ou publier la cause dans ObservabilitySink ; expliciter quelles erreurs personnalisées atteignent le validateur.

**Sources à relire.**

- [src/core/cursiv/syntax-tree/DefaultDocumentReferences.cpp:31](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/syntax-tree/DefaultDocumentReferences.cpp#L31) — Catch-all silencieux hors annulation.
- [src/core/cursiv/syntax-tree/DefaultDocumentReferences.cpp:71](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/syntax-tree/DefaultDocumentReferences.cpp#L71) — L’erreur du résultat n’est pas conservée.
- [src/core/cursiv/syntax-tree/DefaultDocumentReferences.cpp:168](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/syntax-tree/DefaultDocumentReferences.cpp#L168) — Même perte pour MultiRef.
- [src/core/cursiv/validation/DefaultDocumentValidator.cpp:279](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/validation/DefaultDocumentValidator.cpp#L279) — Message reconstruit depuis site et RefState.
- [langium/packages/langium/src/references/linker.ts:220](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/references/linker.ts#L220) — Langium conserve la cause et journalise l’exception simple.

**Contexte REF-009 — Publication d’un résultat de référence.** Un CAS élit le producteur d’une référence. Il écrit le payload avant publication release ; les autres lecteurs utilisent acquire et n’accèdent au résultat qu’après l’état terminal. L’annulation efface les payloads et republie Unresolved pour une nouvelle tentative ; une autre exception publie une erreur après nettoyage. Les candidats sont refiltrés selon le type/narrowing du champ avant stockage. La politique de conservation des causes est cependant plus pauvre que l’API LinkingResult.

**Limite de preuve de ce périmètre.** Lecture statique du code, des interfaces et des callers ; ni exécution ni scénario reproduit.

<a id="finding-ws-ref-overload-span-begin"></a>

### WS-REF-OVERLOAD-SPAN-BEGIN — La récupération d’un groupe de surcharges confond des sites ayant le même début

**P2 · défaut · confiance forte.** Périmètres : REF-014.

**Mécanisme identifié.** getSelfNodes choisit une arête de MultiRef puis relit toutes les références multiples dont begin==incoming.sourceBegin, en ignorant end et incoming.sourceEnd. Si deux sites commencent au même offset mais ont des fins différentes, leurs cibles sont ajoutées au même tableau siblings dès lors que l’un contient le nœud cherché.

**Condition à vérifier.** Une grammaire ou un parser personnalisé produit deux sites de références multiples de même début et de longueurs différentes.

**Impact.** Une déclaration peut être associée à des cibles étrangères à sa MultiRef ; les auto-déclarations/navigation et les opérations qui consomment ce groupe peuvent inclure des symboles sans lien.

**Direction de correction.** Comparer au minimum les deux bornes sourceBegin/sourceEnd ; si plusieurs sites distincts peuvent partager une plage complète, conserver une identité de handle dans les descriptions.

**Sources à relire.**

- [src/core/cursiv/references/DefaultReferences.cpp:166](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/references/DefaultReferences.cpp#L166) — Le paramètre end est ignoré.
- [src/core/cursiv/references/DefaultReferences.cpp:169](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/references/DefaultReferences.cpp#L169) — Rapprochement sur begin seul.
- [src/core/cursiv/workspace/ReferenceDescription.hpp:15](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/ReferenceDescription.hpp#L15) — La description possède déjà les deux bornes.

**Contexte REF-014 — Recherche des usages et auto-déclarations.** Les usages sont pris dans l’index inverse de la cible, filtrés par document puis triés ; l’inclusion de déclaration applique aussi ce filtre aux membres de l’ensemble de surcharges. Le groupe est obtenu depuis la première MultiRef entrante pertinente, y compris d’un autre langage, et chaque auto-déclaration utilise le NameProvider de sa langue. Le rapprochement de la MultiRef source ne compare toutefois que le début du nom, alors que l’index conserve sa plage complète.

**Limite de preuve de ce périmètre.** Lecture statique du code, des interfaces et des callers ; ni exécution ni scénario reproduit.

<a id="finding-ws-ref-scope-extension-drift"></a>

### WS-REF-SCOPE-EXTENSION-DRIFT — Une politique de scope personnalisée doit rester cohérente dans trois chemins d’accès

**P2 · écart d’API · confiance forte.** Périmètres : REF-006.

**Mécanisme identifié.** Cursiv expose lookup unique, visite nommée et visite complète comme opérations indépendantes ; DefaultScopeProvider propose aussi trois hooks globaux distincts. Restreindre uniquement getGlobalScopeEntry ne restreint ni les références multiples ni la complétion.

**Condition à vérifier.** Portage d’une extension Langium qui personnalisait getScope/getGlobalScope pour appliquer des imports, une visibilité ou un namespace.

**Impact.** Davantage de code à adapter et risque concret d’écart entre linking simple, linking multiple et propositions visibles.

**Direction de correction.** Fournir un helper ou une abstraction commune pour définir un ensemble de candidats et en dériver les trois accès ; documenter les trois surcharges à adapter.

**Sources à relire.**

- [src/core/cursiv/references/ScopeProvider.hpp:20](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/references/ScopeProvider.hpp#L20) — Trois opérations publiques.
- [src/core/cursiv/references/DefaultScopeProvider.hpp:55](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/references/DefaultScopeProvider.hpp#L55) — Trois points d’extension globaux.
- [langium/packages/langium/src/references/scope-provider.ts:30](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/references/scope-provider.ts#L30) — Un getScope retourne l’objet commun aux lectures.

**Contexte REF-006 — Énumération des candidats de scope.** Les visites par nom et sans nom appliquent les mêmes filtres de type/narrowing que le lookup. La visite sans nom ignore intentionnellement referenceText pour proposer toute la complétion ; la visite nommée retourne les homonymes de tous les niveaux et enregistre l’import avant le global. L’arrêt anticipé est propagé. Le résultat n’est pas dédupliqué dans ScopeProvider, conformément au contrat ; MultiRef le déduplique ensuite. Les points d’extension séparés exposent un coût d’API.

**Limite de preuve de ce périmètre.** Lecture statique du code, des interfaces et des callers ; ni exécution ni scénario reproduit.

<a id="finding-ws-sch-cancel-linear"></a>

### WS-SCH-CANCEL-LINEAR — L’annulation conserve un parcours linéaire des indices restants

**P2 · limite · confiance forte.** Périmètres : SCH-003.

**Mécanisme identifié.** L’annulation ou l’échec d’un corps évite les prochains corps mais n’arrête pas la réservation et l’itération des indices. Le temps de jonction inclut donc Theta(N restant) appels du wrapper après une annulation précoce.

**Condition à vérifier.** Grand parallelFor à éléments unitaires peu coûteux, annulé ou en échec tôt ; les lots internes de linking/validation réduisent N mais ne changent pas la propriété.

**Impact.** La fin d’annulation peut rester coûteuse même lorsque plus aucun corps utile ne doit démarrer ; aucune durée mesurée n’est avancée.

**Direction de correction.** Permettre au drain d’observer un abandon et d’arrêter nouvelles réservations/tranches tout en acquittant chaque part publiée.

**Sources à relire.**

- [src/core/cursiv/execution/TaskScheduler.cpp:64](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/execution/TaskScheduler.cpp#L64) — Le commentaire confirme que chaque item est encore visité.
- [src/core/cursiv/execution/WorkerPool.cpp:98](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/execution/WorkerPool.cpp#L98) — Boucle fine sans condition d’abandon.
- [src/core/cursiv/execution/WorkerPool.cpp:109](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/execution/WorkerPool.cpp#L109) — Boucle de tranche sans condition d’abandon.

**Contexte SCH-003 — Issue d’un parallelFor interrompu.** Le wrapper attrape toutes les exceptions des corps ; failed.exchange élit un seul auteur de first, lu uniquement après la jonction acquire/release du pool. L’exception enregistrée a priorité sur le contrôle final d’annulation. Les corps déjà actifs ne sont pas préemptés. En revanche, le token et failed n’influencent pas le compteur de réservation de drain : les indices restant à attribuer et ceux de chaque tranche déjà prise sont tous parcourus avec un wrapper devenu vide.

**Limite de preuve de ce périmètre.** La borne est structurelle, sans mesure de latence ; l’issue d’un callback qui ne coopère pas reste dépendante de ce callback.

<a id="finding-ws-sch-nested-spin"></a>

### WS-SCH-NESTED-SPIN — Une attente imbriquée peut entretenir des scans actifs sans travail disponible

**P2 · limite · confiance forte.** Périmètres : SCH-009.

**Mécanisme identifié.** Après le seuil de spin, seuls les demandeurs externes dorment. Un worker attendant un sous-job continue pop, jusqu’à V vols et yield jusqu’à la fin des dernières parts.

**Condition à vérifier.** Un callback lance un parallelFor imbriqué dont quelques dernières parts sont longues ou bloquées ; d’autres workers deviennent des demandeurs imbriqués sans travail à voler.

**Impact.** Coût O(V × nombre de tours) et fences répétées, potentiellement en compétition CPU avec les parts attendues ; aucun deadlock ni temps CPU n’est affirmé sur les charges par défaut.

**Direction de correction.** Introduire un backoff ou une attente informée de l’activité qui conserve une voie de progression des jobs imbriqués ; ne pas garer aveuglément tous les workers.

**Sources à relire.**

- [src/core/cursiv/execution/WorkerPool.cpp:244](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/execution/WorkerPool.cpp#L244) — Balayage de toutes les victimes par tour.
- [src/core/cursiv/execution/WorkerPool.cpp:259](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/execution/WorkerPool.cpp#L259) — Pas de parking pour isWorker.
- [src/core/cursiv/execution/TaskQueue.hpp:85](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/execution/TaskQueue.hpp#L85) — Fence même si la deque est vide.

**Contexte SCH-009 — Attente coopérative d’un job imbriqué.** corun garde un accès à la cellule, relit pending en acquire et peut exécuter d’autres tâches, permettant à un worker d’avancer les sous-jobs dont il attend la fin. Le demandeur externe passe à atomic::wait après 256 tours infructueux et le dernier acquittement notifie la cellule. Le worker imbriqué ne dort pas : chaque nouveau tour balaie jusqu’à victimCount queues puis yield, même lorsqu’il ne reste que des corps longs déjà actifs.

**Limite de preuve de ce périmètre.** Limite structurelle conditionnelle ; l’effet sur latence et CPU n’est pas mesuré.

<a id="finding-ws-svc-registration-partial"></a>

### WS-SVC-REGISTRATION-PARTIAL — Une inscription interrompue peut rester routable tout en disparaissant de all

**P2 · défaut · confiance forte.** Périmètres : SVC-005.

**Mécanisme identifié.** L’inscription renseigne _byLanguageId et les clés avant _all.push_back. Si cette dernière allocation lève pour une nouvelle langue, la langue est possédée et routable, mais absente de all. Une réinscription ultérieure reconnaît son languageId, tente seulement replace dans _all où elle n’existe pas, puis saute push_back : elle ne corrige donc pas cette divergence.

**Condition à vérifier.** Échec d’allocation au dernier push de _all pendant une première inscription, suivi d’une reprise ou réinscription.

**Impact.** Les services de lookup et d’énumération voient des ensembles de langues différents ; une initialisation qui parcourt all peut omettre une langue routable.

**Direction de correction.** Réserver les capacités et préparer les mappings avant publication, ou garantir un rollback complet ; au minimum rendre la reprise capable de restaurer all.

**Sources à relire.**

- [src/core/cursiv/services/DefaultServiceRegistry.cpp:62](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/services/DefaultServiceRegistry.cpp#L62) — languageId publié avant all.
- [src/core/cursiv/services/DefaultServiceRegistry.cpp:85](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/services/DefaultServiceRegistry.cpp#L85) — push_back tardif et uniquement pour première inscription.
- [src/core/cursiv/services/DefaultServiceRegistry.cpp:58](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/services/DefaultServiceRegistry.cpp#L58) — La reprise remplace une entrée supposée présente.

**Contexte SVC-005 — Enregistrement et remplacement d’un langage.** La langue est possédée avant que les tables ne la nomment, évitant un pointeur détruit après une exception. La réinscription du même languageId enlève les anciennes clés, conserve le rang dans all et garde l’ancien conteneur jusqu’à destruction ; une collision entre langues favorise la dernière avec observation. Les tables et all ne sont toutefois pas engagés comme une transaction : un échec peut installer un routage partiel et une reprise ne répare pas nécessairement l’énumération.

**Limite de preuve de ce périmètre.** Lecture statique du graphe de services et des helpers ; les services personnalisés doivent respecter les contrats d’emprunt et de quiescence.

<a id="finding-ws-svc-shared-mismatch"></a>

### WS-SVC-SHARED-MISMATCH — Le registry accepte une langue construite pour un autre workspace

**P2 · écart d’API · confiance forte.** Périmètres : SVC-004.

**Mécanisme identifié.** registerServices vérifie les slots de son propre shared et du conteneur core, mais ne vérifie jamais que services.shared() est ce shared. Les services par défaut ont déjà capturé cette référence à la construction. Une langue construite avec le workspace B peut donc être possédée et routée par le registry A tout en consultant index/documents/scheduler de B.

**Condition à vérifier.** Un hôte utilise directement registerServices avec un CoreServices complet provenant d’un autre SharedCoreServices ; les helpers create_language/register_language ne font pas cette erreur.

**Impact.** Séparation des workspaces rompue et durée de vie empruntée potentiellement invalide si B est détruit alors que A conserve la langue.

**Direction de correction.** Refuser explicitement une identité shared différente lors de l’inscription, ou formaliser et implémenter une opération complète de rebind avant création des services.

**Sources à relire.**

- [src/core/cursiv/services/DefaultServiceRegistry.cpp:24](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/services/DefaultServiceRegistry.cpp#L24) — Complétude contrôlée sans égalité d’identité.
- [src/core/cursiv/services/DefaultServiceRegistry.cpp:44](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/services/DefaultServiceRegistry.cpp#L44) — Ownership acquis sans rebind.
- [src/core/cursiv/services/DefaultCoreService.hpp:15](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/services/DefaultCoreService.hpp#L15) — shared capturé à la construction.
- [src/core/cursiv/services/CoreServices.hpp:42](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/services/CoreServices.hpp#L42) — L’identité shared peut être consultée.

**Contexte SVC-004 — Contrôle des slots requis.** first_missing_service consulte le visiteur virtuel, donc peut inclure les slots requis d’une classe dérivée. Les slots optionnels tels grammarSource n’empêchent pas l’inscription ; le registry refuse un shared/core incomplet et un languageId vide avant transfert dans _owned. Le mécanisme dépend toutefois de déclarations de slots correctes et ne détecte ni les cycles de construction, ni l’identité du shared auquel la langue a été attachée.

**Limite de preuve de ce périmètre.** Lecture statique du graphe de services et des helpers ; les services personnalisés doivent respecter les contrats d’emprunt et de quiescence.

Comparaison incluse : [SVC-004](#comparison-svc-004).

<a id="finding-ws-val-category-commit"></a>

### WS-VAL-CATEGORY-COMMIT — Une exception après publication des diagnostics peut laisser les catégories non acquises

**P2 · défaut · confiance forte.** Périmètres : VAL-009.

**Mécanisme identifié.** validate affecte document.diagnostics avant markAsCompleted. Ce dernier peut allouer dans _validated ou record.ran et lever après avoir mémorisé seulement une partie des catégories. Le document garde tous les nouveaux diagnostics mais la reprise considère encore certaines catégories manquantes.

**Condition à vérifier.** Allocation défaillante lors de markAsCompleted après succès de validateDocument, puis reprise du build dans le même processus.

**Impact.** Une passe relancée peut ajouter une deuxième fois les diagnostics des catégories non marquées ; l’état documentaire et le cache de catégories ne sont pas atomiques.

**Direction de correction.** Préparer le record complet de catégories avant le commit des diagnostics et engager les deux sans allocation après ce point.

**Sources à relire.**

- [src/core/cursiv/workspace/DefaultDocumentBuilder.cpp:713](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultDocumentBuilder.cpp#L713) — Diagnostics engagés avant markAsCompleted.
- [src/core/cursiv/workspace/DefaultDocumentBuilder.cpp:174](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultDocumentBuilder.cpp#L174) — Insertion du record pouvant allouer.
- [src/core/cursiv/workspace/DefaultDocumentBuilder.cpp:179](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultDocumentBuilder.cpp#L179) — Ajouts successifs des catégories.

**Contexte VAL-009 — Mémoire des catégories déjà validées.** Les catégories demandées sont dédupliquées et l’ensemble vide est développé en built_in plus les catégories du registre. Les catégories déjà exécutées ne sont réutilisées que pour la même buildRevision ; reset efface diagnostics et mémoire de catégories ensemble. La validation travaille dans une copie des diagnostics, ce qui protège leur valeur si le validateur lève. Le commit final des diagnostics précède toutefois une allocation dans le registre des catégories.

**Limite de preuve de ce périmètre.** Analyse statique uniquement ; les branches d’exception et coûts structurels ne sont pas des mesures ni des reproductions.

<a id="finding-ws-val-missing-anchor-range"></a>

### WS-VAL-MISSING-ANCHOR-RANGE — Une ancre absente écrase la plage déjà choisie par le builder

**P2 · défaut · confiance forte.** Périmètres : VAL-008.

**Mécanisme identifié.** keyword/property annoncent qu’une ancre absente conserve la plage courante. Or anchorSpan retourne la plage complète du nœud lorsqu’aucune ancre ne correspond, et les setters remplacent systématiquement leur plage par ce résultat non vide. Une chaîne range(plage précise).keyword(nom absent) perd donc son choix de localisation.

**Condition à vérifier.** Le diagnostic a une plage explicite ou une première ancre valide, puis demande une feature/keyword absente du CST de ce nœud.

**Impact.** Diagnostic sur le nœud entier au lieu de la zone choisie ; contrat fluent contradictoire.

**Direction de correction.** Retourner nullopt pour l’absence afin que le builder garde sa plage, ou séparer clairement une API de fallback au nœud et corriger son contrat.

**Sources à relire.**

- [src/core/cursiv/validation/ValidationAcceptor.hpp:156](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/validation/ValidationAcceptor.hpp#L156) — Promesse de conserver la plage si keyword absent.
- [src/core/cursiv/validation/ValidationAcceptor.hpp:211](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/validation/ValidationAcceptor.hpp#L211) — Même promesse pour property.
- [src/core/cursiv/validation/ValidationAcceptor.cpp:112](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/validation/ValidationAcceptor.cpp#L112) — Fallback non vide vers toute la plage du nœud.
- [src/core/cursiv/validation/ValidationAcceptor.hpp:400](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/validation/ValidationAcceptor.hpp#L400) — Résultat non vide remplace _range.

**Contexte VAL-008 — Construction et émission d’un diagnostic.** Le builder possède ses métadonnées, s’émet à la destruction et transmet cette responsabilité lors d’un move ; move-assignment émet d’abord le diagnostic en attente. Le sink et le document sont empruntés pour la passe. emit désarme avant l’appel du sink et normalise end>=begin ; l’émission noexcept absorbe une exception du sink, tandis que accept direct la propage. La source fournie reste préservée. Les ancres mises en cache distinguent CST et NodeId, mais l’absence d’ancre ne respecte pas le maintien de plage documenté.

**Limite de preuve de ce périmètre.** Analyse statique uniquement ; les branches d’exception et coûts structurels ne sont pas des mesures ni des reproductions.

<a id="finding-ws-val-registry-hash-exception"></a>

### WS-VAL-REGISTRY-HASH-EXCEPTION — Un échec de rehash peut laisser le registre avec un multiplicateur incompatible avec sa table

**P2 · défaut · confiance forte.** Périmètres : VAL-001.

**Mécanisme identifié.** entriesOf modifie _mul avant d’appeler rehash. Celui-ci commence par allouer un nouveau vector. Si cette allocation lève, l’ancienne table reste en place avec des positions calculées selon l’ancien multiplicateur, tandis que homeOf/slotOf utilisent déjà le nouveau. Des checks précédemment enregistrés deviennent introuvables après reprise.

**Condition à vérifier.** Une collision déclenche la recherche d’un multiplicateur pendant l’inscription et l’allocation du rehash échoue ; l’hôte conserve le registre après l’exception.

**Impact.** Checks silencieusement sautés ou lookup incohérent dans un registre existant. Les inscriptions réussies sans échec mémoire ne sont pas concernées.

**Direction de correction.** Préparer multiplicateur et nouvelle table en variables temporaires, puis les engager ensemble ; à défaut restaurer _mul si l’allocation échoue.

**Sources à relire.**

- [src/core/cursiv/validation/DefaultValidationRegistry.cpp:140](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/validation/DefaultValidationRegistry.cpp#L140) — Mutation du multiplicateur avant rehash.
- [src/core/cursiv/validation/DefaultValidationRegistry.cpp:117](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/validation/DefaultValidationRegistry.cpp#L117) — Allocation avant remplacement de _byType.
- [src/core/cursiv/validation/DefaultValidationRegistry.cpp:101](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/validation/DefaultValidationRegistry.cpp#L101) — homeOf dépend de _mul courant.
- [src/core/cursiv/validation/DefaultValidationRegistry.cpp:225](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/validation/DefaultValidationRegistry.cpp#L225) — Le chemin rapide peut refuser immédiatement le type.

**Contexte VAL-001 — Enregistrement et indexation des checks.** Les catégories nulles/built_in et les types absents de la réflexion sont rejetés avant l’enregistrement. Un check de base partage une même closure possédée entre toutes les lignes de sous-types ; les captures du helper pointent vers le service validateur, dont les temporaires sont refusés. L’inscription se fait au bootstrap, sans concurrence avec les lecteurs. La recherche de multiplicateur sans collisions possède cependant une rupture de garantie d’exception.

**Limite de preuve de ce périmètre.** Analyse statique uniquement ; les branches d’exception et coûts structurels ne sont pas des mesures ni des reproductions.

Comparaison incluse : [VAL-001](#comparison-val-001).

<a id="finding-ws-val-spelling-quadratic"></a>

### WS-VAL-SPELLING-QUADRATIC — La sélection de trois suggestions peut comparer quadratiquement tous les noms proches

**P2 · limite · confiance forte.** Périmètres : VAL-005.

**Mécanisme identifié.** near_names conserve tous les candidats sous la distance limite et déduplique chaque nouveau nom en parcourant tout le vector near. Pour K noms proches distincts, cette boucle effectue Θ(K²) comparaisons, puis trie K éléments avant de réduire à trois. La limite de 64 sites ne borne pas K et aucune annulation n’est consultée dans cette routine.

**Condition à vérifier.** Un scope contient beaucoup de noms distincts proches du nom introuvable, et une action LSP ou réponse MCP demande des suggestions.

**Impact.** Coût CPU/mémoire potentiellement élevé sur une demande interactive ; aucune latence chiffrée n’est déduite.

**Direction de correction.** Dédupliquer avec un set et maintenir les trois meilleurs candidats pendant la visite, avec un token ou un budget de travail explicite.

**Sources à relire.**

- [src/core/cursiv/validation/SpellingActions.cpp:59](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/validation/SpellingActions.cpp#L59) — Scan de tous les noms déjà retenus.
- [src/core/cursiv/validation/SpellingActions.cpp:65](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/validation/SpellingActions.cpp#L65) — Tri complet avant troncature.
- [src/core/cursiv/validation/SpellingActions.cpp:80](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/validation/SpellingActions.cpp#L80) — API sans token d’annulation.

**Contexte VAL-005 — Diagnostics de linking et suggestions orthographiques.** Les erreurs de linking sont localisées sur le nom et portent le texte, type conteneur et propriété en data ; la cause détaillée perdue est couverte par WS-REF-ERROR-CAUSE-LOST. Les suggestions sont calculées à la demande pour les ErrorNotFound, au maximum sur les 64 premiers sites non vides, dans le scope typé/narrowed du site. Les noms sont classés par distance, localité, puis lexicalement et seules trois actions sont rendues. Cette borne de sortie ne borne pas la collecte des candidats.

**Limite de preuve de ce périmètre.** Analyse statique uniquement ; les branches d’exception et coûts structurels ne sont pas des mesures ni des reproductions.

<a id="finding-ws-wsp-index-retire-order"></a>

### WS-WSP-INDEX-RETIRE-ORDER — Une invalidation qui lève peut laisser un snapshot périmé après mutation de l’index

**P2 · défaut · confiance forte.** Périmètres : WSP-017.

**Mécanisme identifié.** updateContent/removal modifient _exported avant _exports.retire(), et les références suivent le même ordre. Published::publish peut lever lors de _retired.push_back tout en conservant l’ancienne vue publiée. Après l’exception, une lecture à chaud utilise cette ancienne vue sans reconstruire depuis les tables modifiées. Le cas removeContent est durable : une seconde suppression ne trouve plus de ligne et ne retente pas retire.

**Condition à vérifier.** Une vue est déjà publiée et l’allocation de la liste de retrait échoue ; le processus intercepte l’exception et poursuit.

**Impact.** Index incohérent après reprise ; les descriptions d’une ligne retirée peuvent rester visibles. Leurs string_view/nœuds peuvent devenir pendants si le parse est ensuite libéré par le protocole de reset.

**Direction de correction.** Rendre l’invalidation non levante ou garantir son succès avant le commit des tables ; spécifier la garantie d’exception et le chemin de reprise.

**Sources à relire.**

- [src/core/cursiv/workspace/DefaultIndexManager.cpp:144](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultIndexManager.cpp#L144) — Mutation avant retire.
- [src/core/cursiv/workspace/DefaultIndexManager.cpp:217](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultIndexManager.cpp#L217) — Après erase, une nouvelle suppression ne retire plus la vue.
- [src/core/cursiv/workspace/DefaultIndexManager.cpp:238](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultIndexManager.cpp#L238) — Même séquence pour les références.
- [src/core/cursiv/utils/Published.hpp:84](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/utils/Published.hpp#L84) — push_back peut lever avant le remplacement de la vue publiée.

**Contexte WSP-017 — Publication des symboles exportés.** Les descriptions et leur regroupement sont préparés hors mutex puis la ligne est remplacée sous mutex. Les anciens/nouveaux noms affectent les importateurs avant publication. L’ownership du vecteur est assuré par shared_ptr ; les noms et nœuds restent empruntés au parse et exigent le protocole de reset. Une rupture est identifiée dans l’ordre mutation/invalidation en cas d’allocation défaillante.

**Limite de preuve de ce périmètre.** Lecture statique des sources et contrats ; aucune exécution, injection d’allocation défaillante ni mesure de contention.

<a id="finding-ws-wsp-local-null-container"></a>

### WS-WSP-LOCAL-NULL-CONTAINER — Un conteneur nul accepté par LocalSymbols::add rend seal invalide

**P2 · écart d’API · confiance forte.** Périmètres : WSP-022.

**Mécanisme identifié.** add accepte un const AstNode* sans vérifier ni documenter sa non-nullité. seal initialise last à nullptr ; si le premier conteneur est nul, il ignore l’initialisation de met[0] puis incrémente son champ count non initialisé. D’autres conteneurs nuls sont incompatibles avec les sentinelles des tables.

**Condition à vérifier.** Un ScopeComputation personnalisé appelle add(nullptr, description), par exemple pour représenter un scope global ou un nœud détaché.

**Impact.** Lecture de mémoire non initialisée pendant seal ; le service par défaut ne suit pas ce chemin.

**Direction de correction.** Exprimer l’obligation par une référence AstNode&, ou rejeter explicitement nullptr et documenter la précondition au point d’entrée.

**Sources à relire.**

- [src/core/cursiv/workspace/LocalSymbols.hpp:62](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/LocalSymbols.hpp#L62) — Signature pointeur sans précondition non-null.
- [src/core/cursiv/workspace/LocalSymbols.cpp:81](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/LocalSymbols.cpp#L81) — last=nullptr.
- [src/core/cursiv/workspace/LocalSymbols.cpp:96](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/LocalSymbols.cpp#L96) — met[lastMet] utilisé même sans initialisation.

**Contexte WSP-022 — Construction et gel des symboles locaux.** Pour les conteneurs non nuls utilisés par les défauts, seal compte les groupes puis effectue une permutation stable et indexe les noms après le dernier déplacement ; la chaîne des homonymes est construite à rebours pour retrouver l’ordre des déclarations. Les requêtes ne sont autorisées qu’après seal. La construction est temporaire dans ScopeComputation, donc une exception ne remplace pas les symboles documentaires. En revanche, l’API add ne formalise pas l’exclusion de nullptr, pourtant nécessaire à l’algorithme.

**Limite de preuve de ce périmètre.** Lecture statique des sources et contrats ; aucune exécution, injection d’allocation défaillante ni mesure de contention.

<a id="finding-ws-wsp-pending-options"></a>

### WS-WSP-PENDING-OPTIONS — Un build de reprise peut ignorer la nouvelle demande de validation

**P2 · écart d’API · confiance forte.** Périmètres : WSP-007.

**Mécanisme identifié.** Lorsqu’un document possède une demande non completed, prepareBuild ignore entièrement les options du nouvel appel. Après la reprise, l’ancienne demande est achevée mais la nouvelle n’est ni fusionnée ni mise en file. Un build(validation=true, toutes catégories) peut ainsi retourner après avoir seulement honoré un ancien build(validation=false ou catégories restreintes).

**Condition à vérifier.** Une demande précédente est interrompue, puis le même document est rebâti avec une demande différente sans reset effaçant le bookkeeping.

**Impact.** Le caller doit refaire un appel après la reprise pour obtenir sa nouvelle demande ; le retour normal du premier appel ne certifie pas ses options. Ce comportement existe aussi dans Langium.

**Direction de correction.** Documenter explicitement la priorité des demandes ou faire reprendre l’ancienne puis conserver/fusionner la nouvelle ; exposer les catégories effectivement accomplies au caller.

**Sources à relire.**

- [src/core/cursiv/workspace/DefaultDocumentBuilder.cpp:267](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultDocumentBuilder.cpp#L267) — continue si completed=false.
- [src/core/cursiv/workspace/DefaultDocumentBuilder.cpp:413](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultDocumentBuilder.cpp#L413) — Validation avec les options conservées.
- [src/core/cursiv/workspace/DefaultDocumentBuilder.cpp:424](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultDocumentBuilder.cpp#L424) — Ancienne demande marquée complète sans queue de nouvelle demande.

**Contexte WSP-007 — Conservation d’une demande de build.** prepareBuild copie les options par DocumentId et garde une demande inachevée ; getBuildOptions renvoie une copie sous stateMutex. Les demandes finies peuvent être remplacées et resultsAreIncomplete rouvre seulement la validation si des catégories manquent à cette révision. completed n’est posé qu’après les phases, selon Validated ou IndexedReferences si validation=false. La conservation de la demande interrompue signifie cependant qu’un nouvel appel build avec des options plus larges peut terminer la demande ancienne sans conserver les nouvelles options.

**Limite de preuve de ce périmètre.** Conséquence d’API établie par contrôle ; aucun exemple exécuté. La politique est intentionnelle dans les commentaires internes, d’où la classification API et non défaut algorithmique.

<a id="finding-ws-wsp-priority-starvation"></a>

### WS-WSP-PRIORITY-STARVATION — Les priorités n’offrent pas de borne de progression sous flux continu

**P2 · limite · confiance forte.** Périmètres : WSP-014.

**Mécanisme identifié.** Immediate ignore les writers en attente, tandis que Normal attend qu’ils soient tous terminés. Un flux recouvrant de reads Immediate peut empêcher readers de revenir à zéro ; une suite de writes peut repousser indéfiniment Normal.

**Condition à vérifier.** Charge concurrente soutenue avec arrivées recouvrantes de lecteurs Immediate ou de writers.

**Impact.** Aucune latence maximale ni équité interclasses ; les réveils globaux peuvent ajouter de la contention sans rendre le prédicat vrai.

**Direction de correction.** Si l’usage exige une borne, fermer une cohorte de lecteurs devant un writer ou introduire une politique explicite d’équité, sans supprimer les garanties de phase.

**Sources à relire.**

- [src/core/cursiv/workspace/DefaultWorkspaceLock.cpp:137](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultWorkspaceLock.cpp#L137) — Immediate n’examine pas writersPending.
- [src/core/cursiv/workspace/DefaultWorkspaceLock.cpp:142](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultWorkspaceLock.cpp#L142) — Normal exige writersPending nul.
- [src/core/cursiv/workspace/DefaultWorkspaceLock.cpp:67](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultWorkspaceLock.cpp#L67) — Writer exige readers nul.

**Contexte WSP-014 — Admission d’une lecture workspace.** Le seul raccourci réentrant compare writerThread au thread courant ; les autres reads s’inscrivent dans inGate puis attendent selon leur priorité. Normal exige zéro writer en attente ; Immediate attend seulement la fin de mutation du writer actif, mais augmente readers et bloque donc tout prochain writer. Les compteurs sont décrémentés même si l’action lève. Cette politique est sûre pour les entrées non imbriquées, mais crée des cycles si un lecteur Normal se réinscrit derrière un writer, ou si un worker dont le writer attend la jonction reprend un read Normal.

**Limite de preuve de ce périmètre.** Cycles et absence de borne établis par les prédicats ; aucune famine effective ni latence n’a été mesurée.

<a id="finding-ws-wsp-read-reentrance"></a>

### WS-WSP-READ-REENTRANCE — La réentrance de lecture peut fermer un cycle d’attente

**P2 · écart d’API · confiance forte.** Périmètres : WSP-014.

**Mécanisme identifié.** Un read Normal imbriqué n’est pas reconnu comme réentrant. Si un writer est en attente, il attend writersPending=0 alors que le writer attend readers=0 détenu par la première lecture. Un callback de worker du builder prenant Normal attend également le writer dont le parallelFor attend ce worker.

**Condition à vérifier.** Extension faisant une lecture Normal depuis un read existant après arrivée d’un writer, ou depuis un worker d’une phase ; token non annulé.

**Impact.** Cycle d’attente entre l’opération et le writer tant qu’aucune annulation ou fermeture ne le rompt ; aucun défaut de ce type n’est affirmé pour les providers par défaut.

**Direction de correction.** Documenter et faire diagnostiquer les contextes d’accès des extensions ; éviter toute reprise du verrou déjà acquis. Une vraie réentrance reader doit préserver comptage et priorités.

**Sources à relire.**

- [src/core/cursiv/workspace/DefaultWorkspaceLock.cpp:122](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultWorkspaceLock.cpp#L122) — Seul le writer courant contourne le gate.
- [src/core/cursiv/workspace/DefaultWorkspaceLock.cpp:142](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultWorkspaceLock.cpp#L142) — Normal attend la disparition des writers.
- [src/core/cursiv/workspace/DefaultWorkspaceLock.cpp:67](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultWorkspaceLock.cpp#L67) — Writer attend la sortie des lecteurs.
- [src/core/cursiv/workspace/WorkspaceLock.hpp:12](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/WorkspaceLock.hpp#L12) — Avertissements existants n’explicitant pas read-sous-read.

**Contexte WSP-014 — Admission d’une lecture workspace.** Le seul raccourci réentrant compare writerThread au thread courant ; les autres reads s’inscrivent dans inGate puis attendent selon leur priorité. Normal exige zéro writer en attente ; Immediate attend seulement la fin de mutation du writer actif, mais augmente readers et bloque donc tout prochain writer. Les compteurs sont décrémentés même si l’action lève. Cette politique est sûre pour les entrées non imbriquées, mais crée des cycles si un lecteur Normal se réinscrit derrière un writer, ou si un worker dont le writer attend la jonction reprend un read Normal.

**Limite de preuve de ce périmètre.** Cycles et absence de borne établis par les prédicats ; aucune famine effective ni latence n’a été mesurée.

<a id="finding-ws-wsp-symlink-inclusion"></a>

### WS-WSP-SYMLINK-INCLUSION — Le scan initial et les événements peuvent sélectionner différemment un lien symbolique

**P2 · défaut · confiance forte.** Périmètres : TXT-011, WSP-024.

**Mécanisme identifié.** readDirectory utilise symlink_status, ce qui donne isFile=false/isDirectory=false à un lien ; shouldIncludeEntry le rejette. Pour une nouvelle URI signalée comme modifiée, findChangedUris utilise stat, qui suit le lien avec filesystem::status ; le même nom vers un fichier reconnu est alors admis par isIncluded.

**Condition à vérifier.** Lien symbolique de nom non exclu vers un fichier d’une langue enregistrée, absent du store au scan puis annoncé par un événement de fichier.

**Impact.** Le contenu indexé et les diagnostics dépendent de l’historique des notifications plutôt que de la seule politique du workspace.

**Direction de correction.** Définir une politique explicite des symlinks commune au scan et aux événements ; distinguer stat sans suivi ou transmettre la nature de lien au filtre.

**Sources à relire.**

- [src/core/cursiv/workspace/LocalFileSystemProvider.cpp:70](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/LocalFileSystemProvider.cpp#L70) — stat suit la cible.
- [src/core/cursiv/workspace/LocalFileSystemProvider.cpp:110](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/LocalFileSystemProvider.cpp#L110) — readDirectory examine le lien lui-même.
- [src/core/cursiv/workspace/DefaultWorkspaceManager.cpp:45](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultWorkspaceManager.cpp#L45) — Un lien non qualifié file/directory est rejeté.
- [src/core/cursiv/workspace/DefaultDocumentBuilder.cpp:192](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultDocumentBuilder.cpp#L192) — Événement passe par stat et inclusion.

**Contexte TXT-011 — Système de fichiers local.** read_whole vérifie les bornes du conteneur et de streamsize, lit la taille annoncée puis continue jusqu’à EOF : un fichier grandi n’est pas tronqué au stat initial. Une lecture courte à EOF est admise ; bad/fail hors EOF devient FileSystemError. readDirectory vérifie les erreurs d’itération mais ignore une entrée dont le type est inaccessible. stat suit les liens alors que readDirectory expose leur propre type ; cette différence raccordée au builder crée le constat partagé WSP.

**Limite de preuve de ce périmètre.** Les particularités d’encodage natif Windows et les permissions WASI ne sont pas exécutées. Le changement de PWD reste best-effort.

**Contexte WSP-024 — Sélection des ressources d’un workspace.** Le filtre exclut noms vides/cachés, node_modules/out et fichiers sans langue. isIncluded remonte les segments vers les racines pour appliquer le même filtre aux ancêtres, en autorisant aussi une entrée extérieure aux racines selon sa propre admissibilité. searchFolder utilise une pile explicite et continue sur erreur d’un dossier avec observation. Toutefois, le fournisseur local décrit les symlinks différemment dans stat et readDirectory ; le chemin d’une notification nouvelle utilise stat puis isIncluded et n’observe donc pas le même ensemble que le scan.

**Limite de preuve de ce périmètre.** Aucun lien créé ni événement simulé. La divergence est déduite des API filesystem et du flux d’appels ; la disponibilité des événements dépend de l’hôte.

<a id="finding-api-highlight-generation"></a>

### API-HIGHLIGHT-GENERATION — La coloration lexicale livrée est maintenue séparément de la grammaire

**P3 · écart d’API · confiance forte.** Périmètres : API-004.

**Mécanisme identifié.** Le template TextMate contient explicitement person|age|greet et les commentaires/nombres ; l’exemple domainmodel contient sa propre liste de mots-clés. La chaîne de génération C++ et les outils inspectés ne produisent pas de règles TextMate/Monarch/Prism depuis la grammaire.

**Condition à vérifier.** Évolution des mots-clés ou terminaux d’un langage et diffusion à plusieurs éditeurs.

**Impact.** La coloration peut diverger du lexer tant que l’auteur n’actualise pas les artefacts séparés ; ajouter un moteur éditeur exige un export spécifique.

**Direction de correction.** Proposer un export lexical best-effort avec limites explicites et une source commune des artefacts. La procédure actuelle de mise à jour manuelle est déjà documentée ; l’amélioration attendue est son automatisation ou sa centralisation.

**Sources à relire.**

- [templates/project/vscode/syntaxes/@CURSIV_NEW_ID@.tmLanguage.json:32](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/templates/project/vscode/syntaxes/@CURSIV_NEW_ID@.tmLanguage.json#L32) — Mots-clés en dur.
- [examples/domainmodel/vscode/syntaxes/domain-model.tmLanguage.json:14](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/domainmodel/vscode/syntaxes/domain-model.tmLanguage.json#L14) — Liste de coloration indépendante.
- [src/core/cursiv/grammar/GrammarDump.hpp:1](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/grammar/GrammarDump.hpp#L1) — Description de grammaire, sans contrat de générateur de coloration.
- [docs/learn/editor-integration.md:50](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/docs/learn/editor-integration.md#L50) — Maintenance TextMate explicitement à la charge de l’auteur.

**Contexte API-004 — Production de règles de coloration depuis la grammaire.** Les artefacts TextMate sont cohérents avec un usage de squelette, mais ne portent pas de liaison générée aux terminaux C++. Recherche des formats dans src/cmake/tools sans autre générateur dédié. Les semantic tokens sont un service différent : ils requièrent le serveur et ne suppriment pas cette description lexicale distribuée.

**Limite de preuve de ce périmètre.** Pas de jugement d’exactitude pour toutes les regex éditeur ni de comparaison visuelle exécutée.

Comparaison incluse : [API-004](#comparison-api-004).

<a id="finding-api-notebook-lifecycle"></a>

### API-NOTEBOOK-LIFECYCLE — Pas de gestionnaire natif du cycle notebook/cellules

**P3 · écart d’API · confiance forte.** Périmètres : API-005.

**Mécanisme identifié.** Documents gère des URI et documents individuels ; SharedLspServices et DefaultLanguageServer ne proposent pas de gestionnaire de notebook, de relation cellule→notebook ni de handlers de synchronisation notebook. Accepter une URI virtuelle de cellule ne maintient pas son ordre, ses métadonnées ni les événements de structure.

**Condition à vérifier.** Intégration d’un éditeur de notebook utilisant notebookDocument/didOpen,didChange,didClose.

**Impact.** Le consommateur doit ajouter routage, ownership relationnel et synchronisation des cellules avant de définir leurs scopes métier.

**Direction de correction.** Ajouter une brique optionnelle de synchronisation notebook, branchée sur les mêmes écritures ordonnées que les documents texte, et séparer cette brique des règles de visibilité entre cellules.

**Sources à relire.**

- [src/lsp/cursiv/services/SharedLspServices.hpp:28](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/services/SharedLspServices.hpp#L28) — Surface des services partagés.
- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:275](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L275) — Annonce de synchronisation texte.
- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:545](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L545) — Ouverture de document individuel.
- [src/core/cursiv/workspace/Documents.hpp:32](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/Documents.hpp#L32) — Store indexé par URI/document.

**Contexte API-005 — Cycle de vie d’un notebook et de ses cellules.** Recherche de notebook dans src et lecture de la surface LSP/documentaire : les identités URI virtuelles permettent de représenter le texte isolé, sans notion de cellule ordonnée ou de notebook parent. Le point manquant est le cycle structurel et son raccord au store ; la sémantique des dépendances entre cellules relève ensuite du langage.

**Limite de preuve de ce périmètre.** Aucune incompatibilité intrinsèque avec une extension de serveur notebook ; c’est une brique à réaliser. Pas d’essai client.

Comparaison incluse : [API-005](#comparison-api-005).

<a id="finding-ass-cpu-ignore-silencieux"></a>

### ASS-CPU-IGNORE-SILENCIEUX — Le contrôle de croissance CPU LR peut être ignoré sans trace dans le verdict

**P3 · limite · confiance forte.** Périmètres : ASS-019.

**Mécanisme identifié.** La fonction de série retourne sans rien contrôler si les compteurs matériels sont inactifs. En build sans NDEBUG ou pour un échantillon invalide/multiplexé, per_alternative retourne 0 ; cost_stays_flat ignore alors la série entière. Aucun statut explicite ne distingue cette absence de mesure d’un contrôle passé dans le rapport final du test.

**Condition à vérifier.** Exécution en Debug, environnement sans accès PMU, ou mesure invalide/scaled.

**Impact.** Un résultat global vert ne permet pas d’établir que la propriété instructions par alternative a été vérifiée. Les contrôles d’opérations et de correction du même exécutable restent actifs.

**Direction de correction.** Publier les contrôles CPU comme skipped avec raison ; permettre à une lane de performance d’exiger leur disponibilité.

**Sources à relire.**

- [tests/engines/lr/LrGrowth_test.cpp:131](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/tests/engines/lr/LrGrowth_test.cpp#L131) — zéro si non optimisé ou mesure inutilisable
- [tests/engines/lr/LrGrowth_test.cpp:153](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/tests/engines/lr/LrGrowth_test.cpp#L153) — série ignorée sur zéro
- [tests/engines/lr/LrGrowth_test.cpp:292](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/tests/engines/lr/LrGrowth_test.cpp#L292) — sortie si PMU inactif
- [tests/engines/lr/LrGrowth_test.cpp:317](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/tests/engines/lr/LrGrowth_test.cpp#L317) — rapport commun

**Contexte ASS-019 — Preuves de croissance des opérations et du coût CPU.** Les séries LL font doubler les documents et contrôlent acceptation, diagnostics et, dans le helper principal, un premier compteur non nul. Les séries LR séparent nombre de splits/alternatives, cohérence AST de cas ciblés et instructions par alternative. Les générateurs sont construits à l’exécution sur des grammaires contrôlées ; la plupart des ratios vérifient une base non nulle et plusieurs assertions portent sur les résultats exacts (modes, états, ordre des insertions). Le coût CPU LR exclut la réduction en AST et divise par alternatives ou octets : ce n’est pas la latence complète du service. La comparaison de configurations de simulation avec/sans partage vérifie aussi équivalence des lectures et isolement après reset.

**Limite de preuve de ce périmètre.** Analyse statique uniquement ; aucun test ni mesure exécutés. Les compteurs ciblent des opérations choisies et ne prouvent pas une borne sur tout le coût. Les ratios overtakes (GeneratorGrowth:329) et de prédiction LL (LlRepairGrowth:226) n’exigent pas tous une base non nulle ; aucun contre-exemple de régression masquée établi. Le seuil CPU absolu de 0,1 s de KeepFirst dépend de la machine.

<a id="finding-ass-marqueurs-malformes"></a>

### ASS-MARQUEURS-MALFORMES — Des marqueurs de plage mal appariés sont normalisés sans erreur

**P3 · défaut · confiance forte.** Périmètres : ASS-001.

**Mécanisme identifié.** Une fermeture sans ouverture prend0 comme début ; les ouvertures encore présentes dans rangeStack à la fin sont simplement oubliées. Un texte d’attente mal annoté reste donc accepté comme une attente différente de celle écrite.

**Condition à vérifier.** Une fixture oublie une borne ou contient une borne de fermeture supplémentaire.

**Impact.** La faute du test peut déplacer ou supprimer une plage attendue au lieu de rendre la fixture invalide.

**Direction de correction.** Refuser les fermetures orphelines et les ouvertures non fermées avec une ExpectationError indiquant le marqueur et l’offset.

**Sources à relire.**

- [src/test/cursiv/ReplacedIndices.cpp:44](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/test/cursiv/ReplacedIndices.cpp#L44) — début implicite0
- [src/test/cursiv/ReplacedIndices.cpp:51](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/test/cursiv/ReplacedIndices.cpp#L51) — aucune validation finale de pile

**Contexte ASS-001 — Marqueurs de texte et normalisation des positions attendues.** replaceIndices cherche le premier marqueur encore présent, avec priorité de l’index sur l’ouverture de plage au même offset. Les effacements de gauche à droite donnent des offsets dans le texte final et une pile gère les plages imbriquées ; le tri stable les ordonne par début. Les positions et plages LSP sont calculées par TextDocument, donc ces attentes ne constituent pas un oracle indépendant de positionAt/rangeAt pour UTF-8, UTF-16 ou fins de ligne.

**Limite de preuve de ce périmètre.** Analyse statique uniquement ; aucun test exécuté.

<a id="finding-ass-repair-json-budget"></a>

### ASS-REPAIR-JSON-BUDGET — Le JSON des métriques de réparation omet le budget utilisé

**P3 · défaut · confiance forte.** Périmètres : ASS-014.

**Mécanisme identifié.** RepairReport possède repair, construit depuis les quatre paramètres de RepairBudget et affiché par text(). json() sérialise docs_per_class, seed, counter, ceilings et résultats mais jamais repair ni les paramètres structurés du budget.

**Condition à vérifier.** Des rapports --json issus de budgets différents sont conservés ou comparés sans la sortie console.

**Impact.** Le fichier autonome ne permet pas d’identifier une variation déterminante du comportement/coût mesuré.

**Direction de correction.** Sérialiser explicitement le mode de réparation et les quatre valeurs du budget dans le JSON ; conserver aussi révision et identité de corpus.

**Sources à relire.**

- [bench/recovery_metric/main.cpp:311](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/bench/recovery_metric/main.cpp#L311) — budget mis dans repair
- [bench/recovery_metric/support/RepairReport.hpp:43](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/bench/recovery_metric/support/RepairReport.hpp#L43) — présent en texte
- [bench/recovery_metric/support/RepairReport.hpp:116](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/bench/recovery_metric/support/RepairReport.hpp#L116) — absent du JSON

**Contexte ASS-014 — Agrégation, signatures et régénération des résultats attendus.** RepairTally exclut explicitement les noop de toutes les colonnes et calcule les taux à partir des sommes, avec coûts en distribution et comptage des plafonds. Les métriques n’ont pas de seuil de succès global et affichent les classes sans site. Les signatures RecoveryLane sont propres au moteur quand son fichier existe, sinon partagées ; --write met à jour le fichier réellement lu. Les contrôles de plancher subsistent pendant la régénération : monotonie, diagnostic de réparation, AST si accepté, portée du texte, ≥50% des tokens et invention≤4. Les signatures utilisent un multiensemble de textes et ne prouvent pas l’alignement des tokens. Le benchmark construit sa description de budget mais ne la conserve pas dans le JSON.

**Limite de preuve de ce périmètre.** Les témoins refusés sont signalés puis écartés, pas un échec du programme de métriques. astWhole compte aussi total=0 ; les taux à dénominateur nul s’affichent0. Ces dénominateurs doivent être lus avant d’interpréter les pourcentages. Les écritures de référence ne vérifient pas fwrite/fclose ; aucune défaillance IO observée.

<a id="finding-ass-rss-etat-pic"></a>

### ASS-RSS-ETAT-PIC — Le pic RSS est publié sans indiquer si sa lecture et sa remise à zéro ont réussi

**P3 · limite · confiance forte.** Périmètres : ASS-023.

**Mécanisme identifié.** ws_stages ignore le booléen de rss_reset ; la sortie décrit pourtant un build froid et son peak. Si clear_refs est refusé, ce pic reste historique. Par ailleurs rss_now pose valid=true après statm même si getrusage échoue, laissant peak=0, et print_rss_row publie ce zéro comme une mesure.

**Condition à vérifier.** Remise à zéro clear_refs indisponible/refusée ou échec getrusage alors que statm fonctionne.

**Impact.** Un rapport peut attribuer au scénario un pic ancien ou rendre une mesure absente comme0 ; les comparaisons de mémoire ne sont plus autonomes.

**Direction de correction.** Transporter séparément validCurrent, validPeak et peakReset, puis afficher n/a ou préciser pic historique lorsque nécessaire.

**Sources à relire.**

- [src/bench/cursiv/Rss.hpp:28](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/bench/cursiv/Rss.hpp#L28) — fallback silencieux documenté
- [src/bench/cursiv/Rss.hpp:60](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/bench/cursiv/Rss.hpp#L60) — getrusage sans validité séparée
- [src/bench/cursiv/LanguageBench.hpp:614](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/bench/cursiv/LanguageBench.hpp#L614) — reset non conservé
- [src/bench/cursiv/LanguageBench.cpp:433](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/bench/cursiv/LanguageBench.cpp#L433) — peak affiché sous valid global

**Contexte ASS-023 — Lecture et disponibilité des indicateurs de mémoire résidente.** RSS lit le niveau process depuis statm et le pic via getrusage ; les niveaux sont signés afin que les deltas se somment. Les plateformes non Linux ou échecs statm/sysconf retournent invalid. Le delta reprend le pic final absolu, pas un pic incrémental. Le mode resident tente une remise à zéro clear_refs avant le build et imprime les deltas des phases ainsi que peak. Le contrat de validité ne distingue cependant pas les canaux courant/pic ni la réussite du reset.

**Limite de preuve de ce périmètre.** Analyse statique uniquement ; aucune mesure exécutée.

<a id="finding-bld-dependances-npm-hors-suivi"></a>

### BLD-DEPENDANCES-NPM-HORS-SUIVI — La mise à jour automatisée déclarée ne couvre pas les paquets npm du dépôt

**P3 · limite · confiance forte.** Périmètres : BLD-020.

**Mécanisme identifié.** Dependabot ne déclare que github-actions et le second workflow vise FetchContent CMake. Aucune entrée npm ne couvre les manifestes/locks des extensions exemples ni docs/demo ; les versions du template de projet ne sont pas davantage synchronisées par ces mécanismes.

**Condition à vérifier.** Une dépendance Node de l’extension, de son bundler ou de la démo publie une mise à jour.

**Impact.** Ces mises à jour nécessitent une veille et une modification manuelles malgré l’automatisation présente pour les autres dépendances.

**Direction de correction.** Ajouter les répertoires npm effectifs au suivi automatisé et définir une synchronisation des versions exemples/template, avec repacking du scaffold quand nécessaire.

**Sources à relire.**

- [.github/dependabot.yml:2](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/.github/dependabot.yml#L2) — github-actions seul
- [.github/workflows/dependencies.yml:26](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/.github/workflows/dependencies.yml#L26) — checker CMake seulement
- [examples/requirements/vscode/package.json:97](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/requirements/vscode/package.json#L97) — dépendances npm actives
- [docs/demo/package.json:7](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/docs/demo/package.json#L7) — npm de la démo
- [templates/project/vscode/package.json:71](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/templates/project/vscode/package.json#L71) — versions recopiées au scaffold

**Contexte BLD-020 — Mise à jour automatisée des dépendances.** Le job de dépendances CMake s’exécute chaque semaine ou à la demande, avec une action fixée par SHA et permissions d’écriture confinées à la création de PR. La documentation et action.yml de cette révision ont été relus : le mode path=CMakeLists.txt suit include/add_subdirectory et résout les set simples, donc l’emplacement des déclarations sous src et cmake n’est pas à lui seul un défaut. mimalloc est fixé dans une variable de révision et son répertoire patché dépend de cette révision ; une nouvelle version exige toujours la compatibilité du patch. Le commentaire sur l’approbation de PR automatisées concorde avec la documentation GitHub actuelle, qui prévoit des runs pull_request en attente d’approbation pour GITHUB_TOKEN.

**Limite de preuve de ce périmètre.** Analyse statique uniquement ; aucun build, test, générateur, packaging ou workflow exécuté. Références externes lues : https://github.com/dmnq-f/cmake-depcheck/blob/37e4e38626d036f96b698362d69aaa5c530df98d/README.md et action.yml ; https://raw.githubusercontent.com/github/docs/main/data/reusables/actions/actions-do-not-trigger-workflows.md (2026-11-06). Le reverse-resolve d’un SHA dépend de sa correspondance à un tag amont ; aucune correspondance de tags du fork n’est présumée. La régénération du scaffold après évolution des templates reste contrôlée par son test de fraîcheur, pas effectuée par Dependabot.

<a id="finding-bld-frames-arguments-invalides"></a>

### BLD-FRAMES-ARGUMENTS-INVALIDES — Le contrôle de frames réussit avec un seuil non numérique ou sans module

**P3 · défaut · confiance forte.** Périmètres : BLD-023.

**Mécanisme identifié.** Number(limitArg) n’est pas validé et modules peut être vide. Avec un seuil absent/non numérique, limit vaut NaN et toute comparaison bytes > limit est fausse ; avec zéro module, over reste à 0. Dans les deux cas l’outil retourne 0 si le chemin de désassembleur existe.

**Condition à vérifier.** Invocation directe ou future intégration transmettant un seuil invalide ou une liste de modules vide.

**Impact.** Une erreur d’appel désactive le contrôle sans faire échouer la commande.

**Direction de correction.** Exiger au moins un module et un seuil entier fini strictement positif avant de lancer le désassembleur ; retourner un code d’usage distinct sinon.

**Sources à relire.**

- [tools/wasm-frames.mjs:14](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/tools/wasm-frames.mjs#L14) — arguments destructurés sans cardinalité
- [tools/wasm-frames.mjs:19](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/tools/wasm-frames.mjs#L19) — Number non validé
- [tools/wasm-frames.mjs:54](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/tools/wasm-frames.mjs#L54) — comparaison avec NaN
- [tools/wasm-frames.mjs:60](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/tools/wasm-frames.mjs#L60) — succès si over nul

**Contexte BLD-023 — Lecture des tailles de frames Wasm.** Le scanner démarre llvm-objdump sans shell, traite sa sortie en streaming, remet la fenêtre à zéro à chaque fonction et reconnaît global.get / i32.const / i32.sub / global.set de la même globale dans cinq lignes. Il rapporte chaque frame reconnue au-delà du seuil, avec un maximum par module. Un désassembleur absent produit le code 77 et le CTest associé configure bien SKIP_RETURN_CODE=77 ; une erreur de désassemblage est fatale. L’intégration CMake fournit le seuil 16384 et collecte les targets *_lsp des exemples.

**Limite de preuve de ce périmètre.** Analyse statique uniquement ; aucun build, test, générateur, packaging ou workflow exécuté. La reconnaissance ne couvre que ce motif constant, pas allocations dynamiques ni consommation cumulée/récursive. Une forme de prologue différente peut donner zéro frame reconnue et un succès ; aucun module actuel désassemblé dans cet audit.

<a id="finding-bld-identite-virgule"></a>

### BLD-IDENTITE-VIRGULE — FILE_NAMES et EXTENSIONS perdent les virgules appartenant à une valeur

**P3 · écart d’API · confiance forte.** Périmètres : BLD-004.

**Mécanisme identifié.** La liste CMake est aplatie en remplaçant ses points-virgules par des virgules, puis split découpe chaque virgule sans échappement. Une valeur unique FILE_NAMES « my,file » devient deux noms « my » et « file », bien que la virgule soit valide dans un nom de fichier.

**Condition à vérifier.** Un langage revendique un nom exact ou une extension contenant une virgule.

**Impact.** Les métadonnées générées ne décrivent pas le fichier déclaré ; la sélection du langage peut viser les mauvais noms.

**Direction de correction.** Transporter une liste structurée ou un encodage avec échappement ; à défaut, refuser explicitement les caractères non représentables dans l’API CMake.

**Sources à relire.**

- [cmake/CursivGrammar.cmake:32](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/cmake/CursivGrammar.cmake#L32) — aplatit les deux listes
- [src/engines/cursiv/shared/gen/LanguageIdentity.hpp:21](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/shared/gen/LanguageIdentity.hpp#L21) — découpe toute virgule
- [src/engines/cursiv/lr/gen/lr_main.cpp:291](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/gen/lr_main.cpp#L291) — décode les métadonnées LR
- [src/engines/cursiv/ll/gen/ll_main.cpp:111](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/ll/gen/ll_main.cpp#L111) — même décodage LL

**Contexte BLD-004 — Compilation de l’exécutable générateur natif.** Le générateur compile la grammaire via un unique point d’entrée LL/LR, avec includes privés et dépendances explicites core/backend. Les macros transportent nom de header, grammaire, namespace et identité du langage ; l’exécutable est placé dans gen avec un chemin Release plat pour l’hôte. Les limites de compilation spécifiques GCC/Clang sont ciblées par compiler id. -O0/-pipe correspondent aux compilateurs GCC/Clang annoncés, MSVC n’étant pas une plateforme parser annoncée.

**Limite de preuve de ce périmètre.** Analyse statique uniquement ; aucune configuration CMake, compilation ni exécution.

<a id="finding-cgr-option-croissance"></a>

### CGR-OPTION-CROISSANCE — La réécriture des longues suites optionnelles matérialise des suffixes quadratiques

**P3 · risque · confiance forte.** Périmètres : CGR-007.

**Mécanisme identifié.** first_present construit pour chaque position k une nouvelle séquence contenant tout le suffixe k+1..n. Une invocation inscrit donc Θ(n²) références d’enfants avant que les nouvelles branches soient comparées et supprimées. kRunDepth limite les transformations imbriquées, pas la longueur n de cette invocation.

**Condition à vérifier.** Une suite longue d’options déclenche la transformation dès un couple de lectures équivalentes près du début.

**Impact.** Pic de mémoire et travail quadratique pendant la génération, même si le programme final élimine une grande partie des branches. Aucun seuil de latence réel n’a été mesuré.

**Direction de correction.** Représenter les suffixes partagés, ou borner explicitement le travail et le volume de l’expansion avant de créer les séquences.

**Sources à relire.**

- [src/engines/cursiv/shared/gen/TwinBranch.cpp:413](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/shared/gen/TwinBranch.cpp#L413) — La garde porte uniquement sur la profondeur _runs.
- [src/engines/cursiv/shared/gen/TwinBranch.cpp:439](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/shared/gen/TwinBranch.cpp#L439) — Chaque bras copie le suffixe restant dans parts.

**Contexte CGR-007 — Réécriture d’une suite d’éléments optionnels équivalents.** La transformation conserve les options précédant le premier doublon, puis développe le suffixe selon le premier item présent. alike exige des opérandes de même clé, une lecture effective et l’absence de fold ; le code garde les runs inchangés au plafond d’imbrication. La sauvegarde/restauration de _found empêche de publier comme occurrences utilisateur les formes synthétiques. L’ordre fonctionnel est explicite, mais la matérialisation des suffixes produit le coût signalé.

**Limite de preuve de ce périmètre.** La taille totale après plusieurs réécritures imbriquées n’a pas été bornée ; seul le coût d’une invocation est établi.

<a id="finding-cgr-spell-alias"></a>

### CGR-SPELL-ALIAS — Le speller d’opérateurs parcourt récursivement ses alias avec une recherche linéaire de cycle

**P3 · risque · confiance forte.** Périmètres : CGR-008.

**Mécanisme identifié.** spell_rule recherche chaque nouvelle règle dans tout le vecteur _calling, puis appelle récursivement spell sur son corps. Une chaîne de h data rules distinctes menant à un seul littéral entraîne Θ(h²) comparaisons de détection de cycle et une profondeur native Θ(h), alors que le nombre de spellings reste un.

**Condition à vérifier.** Longue chaîne acyclique de data rules employée comme opérateur.

**Impact.** La limite de 64 spellings ne borne ni ce travail ni cette pile ; la génération peut devenir coûteuse ou épuiser la pile pour des chaînes suffisamment profondes. Aucun seuil concret n’est affirmé.

**Direction de correction.** Utiliser des marques de visite et une pile explicite, avec mémorisation des spellings calculés par règle.

**Sources à relire.**

- [src/engines/cursiv/shared/gen/UnaryLevelFault.cpp:106](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/shared/gen/UnaryLevelFault.cpp#L106) — Recherche dans toute la chaîne puis descente récursive.
- [src/engines/cursiv/shared/gen/UnaryLevelFault.cpp:25](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/shared/gen/UnaryLevelFault.cpp#L25) — La constante borne le nombre de spellings, pas les alias.

**Contexte CGR-008 — Énumération bornée des spellings d’opérateurs.** Le produit cartésien de séquence est vérifié contre 64 avant construction ; les choix interrompent l’énumération lorsqu’ils dépassent ce nombre. Les conditions ajoutent une séquence vide, les cycles de data rules rendent un résultat inconnu, et les terminaux non littéraux sont atomiques par identifiant. same_token applique la casse insensible si l’un des littéraux est folded, sans assimiler deux terminaux distincts. Le parcours des alias possède toutefois le coût indépendant du nombre de spellings décrit dans le constat.

**Limite de preuve de ce périmètre.** L’égalité par identifiant des terminaux ne prouve pas la disjonction de leurs langages lexicaux ; cette procédure n’est pas un analyseur général d’intersection de lexers.

<a id="finding-cgr-unary-limite"></a>

### CGR-UNARY-LIMITE — Des niveaux d’opérateurs disjoints peuvent être refusés faute d’énumération

**P3 · limite · confiance forte.** Périmètres : CGR-009.

**Mécanisme identifié.** Si un des deux opérateurs comparés n’est pas énumérable, unary_level_fault émet un GrammarFault gated sans exiger de témoin de collision. Cela comprend les options, répétitions, régions et plus de 64 spellings ; cette politique conservatrice est documentée.

**Condition à vérifier.** Deux niveaux de même famille unaire, ou une paire postfixe/binaire, dont l’un sort du domaine du speller.

**Impact.** La syntaxe DSL ne garantit pas l’acceptation d’une table pourtant disjointe ; l’auteur doit reformuler ses opérateurs ou réduire leurs formes. Ce n’est pas une collision démontrée.

**Direction de correction.** Conserver la distinction gated dans les diagnostics et exposer clairement la limite ; une amélioration optionnelle serait une comparaison symbolique bornée des langages.

**Sources à relire.**

- [src/engines/cursiv/shared/gen/UnaryLevelFault.cpp:173](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/shared/gen/UnaryLevelFault.cpp#L173) — Sélection des paires et refus sans témoin si listed est faux.
- [docs/reference/refusals.md:103](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/docs/reference/refusals.md#L103) — La documentation annonce ce refus conservateur.

**Contexte CGR-009 — Décision de conflit entre niveaux unaires.** Les paires de préfixes ou postfixes sont examinées une seule fois avec b>a ; les paires postfixe/binaire sont examinées dans le sens postfixe. Préfixe/binaire reste autorisé. Pour des listes connues, seul un spelling commun non vide déclenche un conflit ; pour une liste inconnue, gated distingue le refus de capacité. L’index de règle r+1 est cohérent avec l’ordre de _ntRule utilisé par Lowerer::define.

**Limite de preuve de ce périmètre.** Ce contrôle ne tranche pas toute concurrence possible entre terminaux atomiques distincts ; aucun témoin dynamique de grammaire disjointe refusée n’a été construit.

<a id="finding-cgr-warn-identite"></a>

### CGR-WARN-IDENTITE — La déduplication des conditions non bornées utilise un rendu non injectif

**P3 · défaut · confiance forte.** Périmètres : CGR-014.

**Mécanisme identifié.** find_unbounded_runs compare spell(c, run) pour décider que deux runs sont identiques. spell retourne la même chaîne « ... » pour les kinds qu’il ne sait pas rendre, notamment ceux que consumes reconnaît comme consommants. Deux répétitions distinctes de ces corps produisent donc le même texte et peuvent être fusionnées.

**Condition à vérifier.** Au moins deux runs non bornés distincts sous la même règle, avec le même kept et des corps représentés par le fallback de spell, par exemple des formes through différentes.

**Impact.** Un avertissement est perdu ou fusionné avec un autre et son texte ne distingue pas le motif concerné ; le parsing et les tables générées ne sont pas modifiés.

**Direction de correction.** Dédupliquer par identité structurelle canonique ou compléter le rendu avant de l’utiliser comme clé, en gardant le texte uniquement pour la présentation.

**Sources à relire.**

- [src/engines/cursiv/shared/gen/UnboundedCondition.cpp:27](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/shared/gen/UnboundedCondition.cpp#L27) — Des kinds consommants incluent les formes through et same.
- [src/engines/cursiv/shared/gen/UnboundedCondition.cpp:124](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/shared/gen/UnboundedCondition.cpp#L124) — Fallback de rendu commun à tous les kinds non traités.
- [src/engines/cursiv/shared/gen/UnboundedCondition.cpp:142](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/shared/gen/UnboundedCondition.cpp#L142) — Égalité du rendu employée comme identité.

**Contexte CGR-014 — Classification des répétitions non bornées dans les conditions.** Le parcours inspecte les feuilles de gap qui lisent des tokens. consumes évite de signaler une boucle vide ; endless_runs reconnaît kEndless, distingue les tours conservés et n’explore pas inutilement sous un run externe non conservé. Les prédicats positifs/négatifs rouvrent le contexte free, ce qui isole leur répétition. Le contrôle de doublons utilise cependant le texte d’affichage, dont certaines formes sont volontairement abrégées, d’où le constat sur l’identité.

**Limite de preuve de ce périmètre.** L’avertissement de croissance quadratique décrit une structure ; son coût effectif dépend des prédicats émis et n’a pas été mesuré.

<a id="finding-cli-dump-utf8-coupe"></a>

### CLI-DUMP-UTF8-COUPE — La troncature du dump humain peut couper un caractère UTF-8

**P3 · défaut · confiance forte.** Périmètres : CLI-005.

**Mécanisme identifié.** quote_text compte les octets et s’arrête dès written>=60 sans chercher une limite de code point ; les octets>=0x80 sont écrits tels quels.

**Condition à vérifier.** Feuille de plus de60 octets dont un caractère multioctet commence avant le60e et se termine après.

**Impact.** La sortie du dump humain contient une séquence UTF-8 tronquée, généralement affichée comme caractère de remplacement. Le JSON possède un traitement distinct correct.

**Direction de correction.** Tronquer à une limite UTF-8 valide ou échapper les octets de la portion coupée.

**Sources à relire.**

- [src/cli/cursiv/CliUtils.cpp:83](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/cli/cursiv/CliUtils.cpp#L83) — Seuil de60 octets.
- [src/cli/cursiv/CliUtils.cpp:87](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/cli/cursiv/CliUtils.cpp#L87) — Itération par char.
- [src/cli/cursiv/CliUtils.cpp:109](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/cli/cursiv/CliUtils.cpp#L109) — Octets non ASCII émis sans décodage.

**Contexte CLI-005 — Dump textuel et JSON du CST.** Les deux dumps fonctionnent même sans AST et représentent explicitement l’absence d’entrée. Ils utilisent une pile de frames pour conserver l’ordre des enfants sans récursion native ; le nom AST prioritaire est dérivé par ID de CST. Le JSON conserve chaque texte de feuille sans troncature, échappe contrôles et guillemets, remplace l’UTF-8 mal formé, et émet les offsets via to_chars indépendamment de la locale. Le dump humain affiche plages et marqueurs mais sa coupure à60 octets peut casser un caractère UTF-8.

**Limite de preuve de ce périmètre.** Lecture statique uniquement ; aucune exécution du projet ni mesure de coût.

<a id="finding-com-chains-seuil-decalage"></a>

### COM-CHAINS-SEUIL-DECALAGE — Le plafond annoncé des chaînes diffère d’une unité du seuil appliqué

**P3 · défaut · confiance forte.** Périmètres : COM-021.

**Mécanisme identifié.** Le test nchains+1u>=kNoElement refuse l’insertion quand 65534 chaînes existent. Le diagnostic partagé annonce pourtant un besoin de plus de 65535 chaînes ou actions ; une demande de 65535 chaînes suffit déjà à déclencher ce message.

**Condition à vérifier.** Une génération nécessite exactement 65535 chaînes distinctes sans dépasser préalablement la limite des actions.

**Impact.** Une grammaire située à la frontière est refusée plus tôt que le plafond annoncé et l’auteur reçoit une explication numérique inexacte.

**Direction de correction.** Aligner la limite effective et son message ; si 65535 chaînes doivent être autorisées, vérifier tous les consommateurs puis ajuster le test sans ouvrir l’ID sentinelle.

**Sources à relire.**

- [src/engines/cursiv/shared/gen/ChainPool.hpp:155](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/shared/gen/ChainPool.hpp#L155) — Condition effective de refus.
- [src/engines/cursiv/shared/gen/ChainPool.hpp:46](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/shared/gen/ChainPool.hpp#L46) — Plafond décrit par le diagnostic.

**Contexte COM-021 — Identité et bornes d’une chaîne d’actions internée.** L’identité d’une chaîne inclut les actions, la longueur, turn et la documentation ; le hash peut ignorer turn car la comparaison finale le vérifie. Les chaînes absentes et les records vides sont distingués : intern_chain peut retourner kNoRow, bare_chain interne réellement une entrée vide. Les compteurs de chaînes/actions sont testés avant leur incrément, ce qui prévient le rebouclage des IDs dans le chemin normal. Un décalage d’une unité subsiste entre seuil et diagnostic : avec nchains=65534, l’ajout suivant est refusé par nchains+1>=65535, alors qu’il s’agirait de la 65535e chaîne et que le message dit « more than 65 535 ». Le problème est un refus trop tôt/diagnostic de limite inexact, pas un écrasement de mémoire prouvé.

**Limite de preuve de ce périmètre.** Le seuil se déduit de l’arithmétique ; aucun générateur géant n’a été exécuté. Les échecs d’allocation interrompent la génération et ne sont pas assimilés à un état réutilisable garanti.

<a id="finding-com-dynset-cout-creux"></a>

### COM-DYNSET-COUT-CREUX — Le coût d’itération de DynSet dépend aussi de l’étendue des IDs

**P3 · limite · confiance forte.** Périmètres : COM-018.

**Mécanisme identifié.** Le commentaire de Members annonce un coût déterminé par le nombre de membres, quelle que soit leur étendue. Cursor::seek parcourt cependant tous les mots nuls intermédiaires ; le coût réel est O(nombre de mots jusqu’au plus haut bit + nombre de membres).

**Condition à vérifier.** Ensembles peu remplis comportant un ou plusieurs IDs élevés et parcours répétés par members().

**Impact.** Une estimation de coût fondée uniquement sur la cardinalité sous-estime les lectures des ensembles creux ; les choix de représentation ou compteurs de croissance peuvent être mal interprétés.

**Direction de correction.** Documenter le coût réel ; si les consommateurs ont besoin d’un coût proportionnel à la cardinalité, conserver un index des mots non nuls ou choisir une représentation adaptée, après analyse de leurs domaines.

**Sources à relire.**

- [src/engines/cursiv/shared/table/DynSet.hpp:22](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/shared/table/DynSet.hpp#L22) — Promesse de coût indépendante de l’étendue.
- [src/engines/cursiv/shared/table/DynSet.hpp:32](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/shared/table/DynSet.hpp#L32) — Boucle sur les mots nuls.

**Contexte COM-018 — Représentation canonique d’un ensemble dynamique.** Les mutations maintiennent la représentation canonique sans mot nul terminal : set ajoute un bit, reset/intersection/différence appellent trim, et l’union d’entrées canoniques conserve nécessairement un dernier mot non nul. Cela justifie l’égalité vectorielle, any/none et le rejet rapide d’inclusion par longueur. rank masque correctement les bits au-dessus de i sans décaler de 64. L’itération est ordonnée et retire le bit faible à chaque incrément. Sa complexité ne dépend toutefois pas uniquement du nombre de membres comme annoncé : seek traverse un à un les mots nuls entre deux bits. Un ensemble très creux conserve donc une dépendance au plus haut ID.

**Limite de preuve de ce périmètre.** L’exactitude ensembliste est établie à la lecture ; la différence de complexité est analytique, sans mesure sur un corpus.

<a id="finding-core-json-utf8"></a>

### CORE-JSON-UTF8 — JsonValue écrit les octets non UTF-8 sans politique explicite

**P3 · écart d’API · confiance forte.** Périmètres : PUB-005.

**Mécanisme identifié.** Les chaînes sont copiées dans un std::string sans validation ; append_escaped_json_string échappe les contrôles ASCII mais émet tous les octets >=0x20 tels quels. Une chaîne mal formée en UTF-8 produit donc un flux toJsonString qui n’est pas du JSON UTF-8 valide.

**Condition à vérifier.** Payload Diagnostic::data ou autre JsonValue contenant des octets invalides, par exemple repris d’un texte source non valide.

**Impact.** Le consommateur JSON peut refuser le payload malgré un JsonValue construit avec succès.

**Direction de correction.** Définir le contrat UTF-8 : rejeter à la frontière ou remplacer les sous-parties invalides lors de la sérialisation, et documenter ce choix.

**Sources à relire.**

- [src/core/cursiv/services/JsonValue.cpp:23](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/services/JsonValue.cpp#L23) — Échappement limité aux caractères ASCII spéciaux.
- [src/core/cursiv/services/JsonValue.cpp:173](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/services/JsonValue.cpp#L173) — Constructeurs chaîne sans validation.

**Contexte PUB-005 — Valeurs JSON du noyau et propriété de leur contenu.** Le variant possède chaînes, tableaux et objets ; copie profonde par valeur et affectation par paramètre copié protègent le cas où la source est contenue dans la destination. Les entiers int64 restent des entiers jusqu’à la sérialisation ; number les convertit volontairement en double. NaN et infinis deviennent null. Les clés sont triées pour une sortie déterministe et les doubles utilisent la locale classic avec max_digits10. Les accesseurs exigent la bonne alternative, conformément au header.

**Limite de preuve de ce périmètre.** L’arbre JSON a une sérialisation et une destruction récursives ; sa profondeur est à maîtriser par l’extension qui le construit. Aucun parseur JSON ni serializer AST n’est fourni par ce type.

<a id="finding-core-obs-nul"></a>

### CORE-OBS-NUL — Une observation contenant NUL est tronquée sur stderr

**P3 · défaut · confiance forte.** Périmètres : PUB-007.

**Mécanisme identifié.** append_flat ne transforme que CR et LF, puis StderrObservabilitySink utilise fputs(line.c_str()). Un NUL dans message, URI ou languageId arrête donc la sortie alors que la chaîne formatée contient encore du texte.

**Condition à vérifier.** Observation contenant un NUL embarqué, admis par les std::string du modèle.

**Impact.** Perte de la fin du message et éventuellement des champs URI/langue nécessaires à l’identification.

**Direction de correction.** Échapper les octets de contrôle ou écrire la longueur complète avec fwrite ; choisir un format explicite pour les champs.

**Sources à relire.**

- [src/core/cursiv/observability/ObservationFormat.cpp:11](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/observability/ObservationFormat.cpp#L11) — NUL conservé dans le format.
- [src/core/cursiv/observability/StderrObservabilitySink.cpp:18](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/observability/StderrObservabilitySink.cpp#L18) — fputs arrête au premier NUL.

**Contexte PUB-007 — Rendu et identification des observations.** Chaque code/severity connu dispose d’un nom stable et un enum inconnu donne ?. Le format commence par gravité et code, puis message et champs optionnels URI/langue ; CR/LF sont aplatis pour garder une ligne. Les champs ne sont pas quotés, donc ce rendu humain ne forme pas un format machine non ambigu. Le sink reçoit toutefois l’Observation structurée avant formatage et peut choisir une autre représentation.

**Limite de preuve de ce périmètre.** L’absence de timestamp ou d’identifiant de requête est une limite d’observabilité configurable par un sink, pas une perte du code structuré.

<a id="finding-core-unicode-version"></a>

### CORE-UNICODE-VERSION — Les tables Unicode ne portent pas une version de génération reproductible

**P3 · limite · confiance forte.** Périmètres : TXT-003.

**Mécanisme identifié.** Le générateur adopte unicodedata.unidata_version de l’interpréteur hôte et télécharge DerivedCoreProperties correspondant. Les en-têtes produits n’inscrivent pas cette version ; CaseIgnorable ajoute une liste manuelle de propriétés Word_Break.

**Condition à vérifier.** Maintenance ou régénération sur des versions de Python différentes.

**Impact.** Un changement d’environnement peut modifier les classes lexicales et la casse sans changement explicite de version Unicode ; l’origine exacte des tables commitées est difficile à vérifier.

**Direction de correction.** Fixer la version Unicode et la source des données, inscrire version et empreintes dans les headers, dériver aussi les propriétés contextuelles de cette version.

**Sources à relire.**

- [tools/gen_unicode_ranges.py:43](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/tools/gen_unicode_ranges.py#L43) — Version prise à l’hôte.
- [tools/gen_unicode_ranges.py:118](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/tools/gen_unicode_ranges.py#L118) — Liste manuelle des caractères Word_Break.
- [tools/gen_unicode_ranges.py:123](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/tools/gen_unicode_ranges.py#L123) — En-tête de génération sans version.

**Contexte TXT-003 — Génération et bornes des tables Unicode.** Les ranges sont coalescées à partir de codepoints triés ; les catégories majeures excluent Cs, XID_Start écarte explicitement underscore et White_Space retire U+001C–001F du résultat Python. within recherche la première borne haute >= cp, puis vérifie la borne basse ; il suppose les intervalles triés et disjoints. Les runs de minuscules encodent delta/stride et isolent l’expansion U+0130. Deux défauts de maintenance de la chaîne de génération sont établis ci-dessous.

**Limite de preuve de ce périmètre.** Aucune régénération ni comparaison exhaustive avec les fichiers UCD n’a été exécutée ; aucune erreur de cellule particulière n’est affirmée.

<a id="finding-exa-req-rapport-cout"></a>

### EXA-REQ-RAPPORT-COUT — Le rapport relit toutes les références de tests pour chaque exigence

**P3 · limite · confiance forte.** Périmètres : EXA-019.

**Mécanisme identifié.** Le générateur boucle sur les exigences puis tous les modèles et tests, et appelle any_of sur les références de chaque test. La table inverse déjà utilisée par la validation n’est pas reconstruite/reprise pour le rapport.

**Condition à vérifier.** R exigences et T tests avec peu de liens chacun ; R et T augmentent ensemble.

**Impact.** Coût O(R×nombre total de références de tests), potentiellement quadratique malgré un rapport de taille linéaire dans les liens de couverture.

**Direction de correction.** Construire une table inverse Requirement→tests en une passe, puis écrire les lignes dans l’ordre du modèle.

**Sources à relire.**

- [examples/requirements/src/cli/requirements/Generator.cpp:62](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/requirements/src/cli/requirements/Generator.cpp#L62) — Boucle par exigence.
- [examples/requirements/src/cli/requirements/Generator.cpp:68](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/requirements/src/cli/requirements/Generator.cpp#L68) — Parcours de tous les tests.
- [examples/requirements/src/cli/requirements/Generator.cpp:77](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/requirements/src/cli/requirements/Generator.cpp#L77) — Recherche répétée dans les références.

**Contexte EXA-019 — Périmètre et contenu du rapport HTML de couverture.** La commande scanne le dossier source et rassemble tous les TestModel du store ; le générateur produit une ligne par exigence du modèle demandé et un bloc par test dont une référence résolue vise exactement cette exigence. Une référence répétée dans un test ne le duplique pas, grâce à any_of ; sans couverture la cellule reste vide. Les chemins libres sont échappés pour le contexte texte HTML (&,<,>) ; les noms proviennent d’ID et ne nécessitent pas d’échappement supplémentaire pour les modèles syntaxiquement valides. Les div sont correctement fermés, contrairement au template Langium lu. Le coût répète le scan pour chaque exigence et la commande ne refuse pas les documents en erreur.

**Limite de preuve de ce périmètre.** Lecture statique uniquement ; aucune exécution du projet ni mesure de coût.

<a id="finding-exa-sm-transitions-cout"></a>

### EXA-SM-TRANSITIONS-COUT — La déduplication des transitions est quadratique par état

**P3 · limite · confiance forte.** Périmètres : EXA-023.

**Mécanisme identifié.** distinct_event_transitions garde un vector de noms vus et le recherche linéairement à chaque transition. Les passes de déclaration et définition recalculent la même sélection.

**Condition à vérifier.** Un état possédant N transitions avec N événements distincts.

**Impact.** Environ N(N−1)/2 comparaisons par passe, doublées par la génération ; coût non borné par un budget propre à l’exemple.

**Direction de correction.** Conserver l’ordre par vector de résultats mais utiliser un set pour l’appartenance, et réutiliser la sélection entre les deux passes.

**Sources à relire.**

- [examples/statemachine/src/cli/statemachine/Generator.cpp:41](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/statemachine/src/cli/statemachine/Generator.cpp#L41) — Sélection par vector de noms.
- [examples/statemachine/src/cli/statemachine/Generator.cpp:50](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/statemachine/src/cli/statemachine/Generator.cpp#L50) — Recherche linéaire.
- [examples/statemachine/src/cli/statemachine/Generator.cpp:143](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/statemachine/src/cli/statemachine/Generator.cpp#L143) — Première sélection.
- [examples/statemachine/src/cli/statemachine/Generator.cpp:157](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/statemachine/src/cli/statemachine/Generator.cpp#L157) — Seconde sélection identique.

**Contexte EXA-023 — Sélection d’une transition par événement et génération des handlers.** La sélection est explicitement première transition résolue par nom d’événement. Les nœuds nuls ou références absentes sont ignorés ; un doublon ultérieur ne génère donc pas une seconde méthode. Déclarations et définitions appellent le même helper avec le même AST, ce qui garantit leur accord. Un état sans transition hérite des handlers affichant Impossible event. La priorité du premier doublon est déterministe mais ne fait pas l’objet d’un diagnostic. Le vector des noms vus induit un coût quadratique, et les références interdocuments peuvent produire un override absent du State de base.

**Limite de preuve de ce périmètre.** Lecture statique uniquement ; aucune exécution du projet ni mesure de coût.

<a id="finding-host-extension-duplication"></a>

### HOST-EXTENSION-DUPLICATION — La sélection de serveur est dupliquée avec des priorités difficiles à maintenir

**P3 · écart d’API · confiance forte.** Périmètres : HOST-019.

**Mécanisme identifié.** Les4 extensions d’exemple répliquent les mêmes helpers et ne diffèrent que par noms/commentaires ; le template en a une autre version. La variable CURSIV_<LANG>_SERVER n’est lue que dans le fallback natif, après preset/Wasm, bien que le commentaire du résolveur présente environnement avant build. Une valeur configurée native force un autre chemin que la détection Wasm de preset.

**Condition à vérifier.** Maintenance de la sélection multiplateforme ou usage de la variable d’environnement alors qu’un Wasm existe déjà.

**Impact.** Un override d’environnement peut être ignoré ; chaque correction doit être propagée dans plusieurs copies et le squelette suit une autre convention de settings.

**Direction de correction.** Centraliser une source copiée au packaging avec options de disposition, et documenter/tester la priorité globale exacte configuration→preset→package→environnement→build selon la décision retenue.

**Sources à relire.**

- [examples/domainmodel/vscode/src/extension.ts:69](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/domainmodel/vscode/src/extension.ts#L69) — Préférence Wasm.
- [examples/domainmodel/vscode/src/extension.ts:129](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/domainmodel/vscode/src/extension.ts#L129) — Environnement seulement dans résolveur natif.
- [examples/domainmodel/vscode/src/extension.ts:159](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/domainmodel/vscode/src/extension.ts#L159) — Ordre global réel.
- [templates/project/vscode/src/extension.ts:95](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/templates/project/vscode/src/extension.ts#L95) — Variante du template.

**Contexte HOST-019 — Résolution de la commande serveur VS Code.** La lecture des4 premiers octets distingue le binaire du Wasm pour le preset, ferme le fd dans finally et retombe explicitement sur le runner embarqué. Les candidats natifs sont dédupliqués ; un nom sans séparateur est laissé à PATH, un chemin doit exister. Le template cherche bin puis ../build/src, tandis que les exemples remontent à leur dépôt et build/examples/<key>/src. Les différences ont été contrôlées par comparaison textuelle des4 copies et du template, pas supposées depuis leur nom.

**Limite de preuve de ce périmètre.** Le setting est documenté comme exécutable natif ; l’absence de détection Wasm sur une valeur native configurée n’est pas signalée comme défaut. Les contraintes Node22/VSCode1.101 sont documentées et le packaging relève le manifeste, vérifié dans BLD-012.

<a id="finding-host-memory-reader-drift"></a>

### HOST-MEMORY-READER-DRIFT — Les deux lecteurs d’imports Wasm ont divergé

**P3 · écart d’API · confiance forte.** Périmètres : HOST-002.

**Mécanisme identifié.** Le navigateur affirme employer le même lecteur que Node, mais son LEB n’a pas les bornes et contrôles du lecteur Node, les tags d’exception et références typées ne sont pas sautés correctement, et les flags de mémoire ne sont pas contrôlés. Il force ensuite shared:true et importe seulement env.memory. Node accepte les espaces de noms de mémoire réels, distingue shared et maximum absent, et refuse memory64 explicitement.

**Condition à vérifier.** Module valide hors du profil threaded wasm32 env.memory annoncé par la démo, par exemple tag importé avant mémoire (fourni via host-module côté Node) ou mémoire non partagée.

**Impact.** Les profils acceptés par les deux hôtes diffèrent ; la démo peut refuser un module que Node démarre avec les imports applicatifs requis. Aucun échec des artefacts du profil publié n’est affirmé. Le commentaire « même lecteur » masque cette divergence.

**Direction de correction.** Partager le décodage borné ou déclarer/vérifier le profil strict accepté par la démo avant de créer ses workers.

**Sources à relire.**

- [tools/wasi-run.mjs:171](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/tools/wasi-run.mjs#L171) — Lecteur borné, types et tags.
- [docs/demo/server.js:11](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/docs/demo/server.js#L11) — Copie simplifiée divergente.
- [docs/demo/server.js:50](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/docs/demo/server.js#L50) — Shared imposé.
- [docs/demo/wasi-worker.js:289](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/docs/demo/wasi-worker.js#L289) — Import env.memory fixe.

**Contexte HOST-002 — Décodage de la mémoire importée Wasm.** Le lecteur Node borne chaque octet à la section et refuse les LEB u32 hors domaine. Il lit le premier import mémoire, ce qui est cohérent avec le refus préalable de plusieurs imports. Dans les deux chemins de chargement, WebAssembly.compile valide d’abord les bytes : les faiblesses du lecteur web ne sont donc pas présentées comme une exploitation de binaire malformé. Le problème établi est la divergence sur des formes valides ou sur le profil mémoire.

**Limite de preuve de ce périmètre.** Aucun module alternatif compilé ni chargé. Le profil des artefacts habituels reste compatible avec la démo.

<a id="finding-host-web-fd-thread-local"></a>

### HOST-WEB-FD-THREAD-LOCAL — Le filesystem WASI web ne partage pas les descripteurs entre pthreads

**P3 · écart d’API · confiance forte.** Périmètres : HOST-007.

**Mécanisme identifié.** fds est une Map locale au module JS de chaque worker et nextFd part d’une plage propre. Ouvrir sur un worker puis lire/fermer sur un autre ne retrouve pas le même descripteur ; le partage de mémoire Wasm ne partage pas cette Map.

**Condition à vérifier.** Programme qui transmet un FILE/fd entre pthreads, ou fermeture sur un autre thread.

**Impact.** EBADF ou comportement divergent du modèle de descripteurs d’un processus ; l’usage de scan local à chaque tâche fonctionne tant qu’il ne traverse pas cette frontière.

**Direction de correction.** Documenter ce sous-ensemble pour la démo ou implémenter une table de descripteurs partagée/centralisée avec offsets synchronisés.

**Sources à relire.**

- [docs/demo/wasi-worker.js:21](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/docs/demo/wasi-worker.js#L21) — Registre local par worker.
- [docs/demo/wasi-worker.js:54](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/docs/demo/wasi-worker.js#L54) — Lecture via la Map locale.
- [docs/demo/wasi-worker.js:303](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/docs/demo/wasi-worker.js#L303) — Plage initiale distincte.

**Contexte HOST-007 — Filesystem virtuel de la démo.** Le filesystem est une copie en lecture seule des fichiers d’exemple. lookup synthétise les dossiers, les lectures bornent à EOF et conservent l’offset, readdir encode noms UTF8 et cookies avec records partiels. path_open refuse création/exclusif/troncature et vérifie O_DIRECTORY. Les champs de taille/stat respectent le layout utilisé. L’ABI reste volontairement minimale : droits annoncés tous positionnés malgré le readonly, close accepte un fd inconnu, whence invalide dans seek n’est pas transformé en EINVAL. resolve traite les segments lexicalement et suppose que les fichiers fournis sont sous la racine.

**Limite de preuve de ce périmètre.** La racine, les fichiers et les appels proviennent de la démo ; aucune garantie de confinement d’un module hostile n’est revendiquée. Les descripteurs cross-thread constituent une limite distincte explicite.

<a id="finding-host-web-plaintext-markdown"></a>

### HOST-WEB-PLAINTEXT-MARKDOWN — Les contenus LSP plaintext sont interprétés comme Markdown

**P3 · écart d’API · confiance forte.** Périmètres : HOST-016.

**Mécanisme identifié.** markdown transforme tout MarkupContent ayant kind en {value:c.value}, sans distinguer plaintext de markdown. Cette valeur est livrée au renderer Markdown Monaco.

**Condition à vérifier.** Hover ou documentation de complétion en plaintext contenant des marqueurs Markdown.

**Impact.** Le texte affiché peut être mis en forme, masqué ou rendu comme lien au lieu de conserver sa représentation littérale.

**Direction de correction.** Échapper le Markdown pour kind=plaintext ou produire une représentation Monaco réellement textuelle ; conserver les MarkedString de code dans leur branche dédiée.

**Sources à relire.**

- [docs/demo/lsp.js:14](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/docs/demo/lsp.js#L14) — kind est testé mais sa valeur ignorée.
- [docs/demo/lsp.js:90](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/docs/demo/lsp.js#L90) — Usage du résultat dans hover.

**Contexte HOST-016 — Conversion des résultats de lecture Monaco.** Les coordonnées font le passage0→1 pour lignes/colonnes UTF16, sans changement d’unité. Location et LocationLink ciblent une URI et la sélection ; les hiérarchies DocumentSymbol sont converties récursivement, avec conversion des enums1-based. Le voisinage, hover et references sont couverts. La réduction des LocationLink perd originSelectionRange et l’étendue cible complète ; les symboles plats perdent containerName, tags sont vidés et foldingRange ne conserve ni kind ni colonnes. Ces pertes sont des limites de l’adaptateur de démo, pas du provider serveur.

**Limite de preuve de ce périmètre.** Aucune inspection visuelle. La récursion de conversion des symboles a une profondeur égale à la hiérarchie renvoyée ; pas de borne ajoutée par cet adaptateur.

<a id="finding-lex-front-insertions"></a>

### LEX-FRONT-INSERTIONS — Insertion quadratique possible des curseurs concurrents

**P3 · risque · confiance forte.** Périmètres : LEX-020.

**Mécanisme identifié.** PredicateStates::add insère dans un tail trié de vecteur. k endpoints nouveaux dans l’ordre décroissant déplacent 1+2+…+(k−1) curseurs. Les reconnaisseurs alimentent add dans l’ordre des alternatives. Le merge de unite ne couvre pas ces ajouts individuels.

**Condition à vérifier.** Front large d’endpoints, par exemple alternatives écrites de la plus longue à la plus courte.

**Impact.** Temps quadratique en largeur du front, sans perte de résultat démontrée.

**Direction de correction.** Collecter puis trier/dédupliquer les endpoints en bloc, en conservant le curseur inline pour le cas déterministe.

**Sources à relire.**

- [src/engines/cursiv/shared/runtime/PredicateState.hpp:50](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/shared/runtime/PredicateState.hpp#L50) — Insertion triée.
- [src/engines/cursiv/shared/runtime/LexicalMachine.hpp:146](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/shared/runtime/LexicalMachine.hpp#L146) — Ajouts dans l’ordre des mots.

**Contexte LEX-020 — Ensembles de curseurs, répétitions et résultats mémorisés.** Les curseurs se comparent sur toutes leurs composantes. Le tail reste trié et distinct de l’inline ; unite fusionne puis retire les doublons. Les répétitions explorent les états absents de visited. Le cache des rounds s’utilise seulement avant dispersion, sans token tenu ni gap antérieur, et agrège verdict/portée en remontant le chemin.

**Limite de preuve de ce périmètre.** visited assure la terminaison du domaine fini, sans quota mémoire.

<a id="finding-ll-cellule-appels-scan"></a>

### LL-CELLULE-APPELS-SCAN — L’admission des callers d’une cellule rescane ses membres

**P3 · risque · confiance forte.** Périmètres : LL-030.

**Mécanisme identifié.** Chaque appel ajouté à une cellule commune invoque holds, un any_of linéaire sur tous les membres précédents.

**Condition à vérifier.** k piles distinctes atteignent la même règle partagée avec même root/tag/kernel pendant une fermeture.

**Impact.** Θ(k²) comparaisons pour former une cellule pourtant destinée à économiser le travail partagé.

**Direction de correction.** Indexer les piles présentes dans les cellules à forte fan-in, tout en gardant l’ordre des membres pour la préférence.

**Sources à relire.**

- [src/engines/cursiv/ll/runtime/LlStackSet.hpp:62](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/ll/runtime/LlStackSet.hpp#L62) — recherche linéaire
- [src/engines/cursiv/ll/runtime/LlEpisodeSim.hpp:805](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/ll/runtime/LlEpisodeSim.hpp#L805) — recherche avant chaque ajout

**Contexte LL-030 — Cellules de piles et partage d’appels.** Les cellules conservent la pile et les deux historiques de chaque membre ; la racine est portée par le thread et incluse dans la clef de partage. La fermeture scelle les dépendances avant la cellule, sans supposer l’ordre de création topologique. Les fusions excluent les piles ancêtres apparentées, aplatissent les cellules singleton et reconstituent les historiques à la sortie.

**Limite de preuve de ce périmètre.** Revue statique ; aucune exécution.

<a id="finding-ll-dfa-cap-depassement"></a>

### LL-DFA-CAP-DEPASSEMENT — Le plafond de 512 états n’est pas un plafond d’allocation

**P3 · risque · confiance forte.** Périmètres : LL-011.

**Mécanisme identifié.** kExploreCap est vérifié avant l’expansion d’un état non résolu. Une seule expansion peut interner plus de 512 successeurs ; les successeurs résolus sont ensuite sautés avant le contrôle.

**Condition à vérifier.** Décision avec alphabet assez large créant de nombreux successeurs résolus en une expansion.

**Impact.** La table et le travail peuvent dépasser le plafond apparent ; effet de ressources, pas défaut sémantique établi.

**Direction de correction.** Vérifier le plafond au point d’interning ou renommer/documenter la borne comme seuil d’expansion.

**Sources à relire.**

- [src/engines/cursiv/ll/gen/ll_build.cpp:1427](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/ll/gen/ll_build.cpp#L1427) — seuil annoncé
- [src/engines/cursiv/ll/gen/ll_build.cpp:1493](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/ll/gen/ll_build.cpp#L1493) — ordre des contrôles et interning

**Contexte LL-011 — Classification des décisions et construction du DFA de regard.** Les ensembles de configurations sont triés et dédupliqués exactement. Le chemin single exige des FIRST disjoints et aucun filtre pertinent ; staged garde les ambiguïtés ou conditions résiduelles pour adaptive. La fermeture a une limite séparée de 4096. Les états sans issue vers un bras unique deviennent handover sans retirer leur terminal de l’alphabet.

**Limite de preuve de ce périmètre.** Revue statique ; aucun test ni génération exécuté.

<a id="finding-ll-episode-tls-retention"></a>

### LL-EPISODE-TLS-RETENTION — Les buffers d’épisode retiennent leur capacité maximale par thread

**P3 · risque · confiance forte.** Périmètres : LL-026.

**Mécanisme identifié.** Les vecteurs du moteur TLS, dont _spare et ses vecteurs imbriqués, survivent au parse ; release remet _steps à zéro et libère le scratch mais ne réduit pas ces capacités.

**Condition à vérifier.** Un parse avec un front volumineux sur chaque thread durable d’un serveur, éventuellement pour plusieurs tables de langage.

**Impact.** Mémoire conservée après les documents coûteux, multipliée par threads et instanciations de table ; la destruction des caches de session ne libère pas ces buffers du moteur.

**Direction de correction.** Appliquer un seuil de rétention aux buffers TLS à la sortie du parse, en traitant aussi les vecteurs imbriqués.

**Sources à relire.**

- [src/engines/cursiv/ll/runtime/LlEpisode.hpp:225](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/ll/runtime/LlEpisode.hpp#L225) — release sans remise de capacité
- [src/engines/cursiv/ll/runtime/LlEpisode.hpp:1593](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/ll/runtime/LlEpisode.hpp#L1593) — positions/spares conservés
- [src/engines/cursiv/ll/runtime/LlEpisode.hpp:1676](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/ll/runtime/LlEpisode.hpp#L1676) — moteur TLS

**Contexte LL-026 — Sessions, réentrance et ressources TLS.** La session publique détient exact/lent/sim/plan/mémo et restaure son prédécesseur par RAII, y compris en cas d’exception. release n’abandonne qu’un épisode détenu par la session sortante. Les buffers de l’objet LlEpisode restent cependant thread_local ; sa sortie n’en rend pas la capacité.

**Limite de preuve de ce périmètre.** Le trim des LlEpisodeSim et LlHistory est distinct et ne couvre pas les buffers de LlEpisode. Aucun défaut de réentrance démontré sur les callbacks AST.

<a id="finding-ll-front-deplacements"></a>

### LL-FRONT-DEPLACEMENTS — Le front trié d’épisode a un coût quadratique en positions coexistantes

**P3 · risque · confiance forte.** Périmètres : LL-032.

**Mécanisme identifié.** drive efface systématiquement le premier élément du vecteur ; insert recherche par lower_bound mais insère au milieu, déplaçant les positions restantes.

**Condition à vérifier.** Front de k positions lexicales concurrentes maintenues jusqu’à leur visite ; par exemple de nombreuses longueurs de lecture encore viables.

**Impact.** Θ(k²) déplacements pour vider ce front, indépendamment du coût de simulation ; la recherche logarithmique ne borne pas le déplacement.

**Direction de correction.** Employer une structure ordonnée avec extraction minimale ou une file avec index de tête et stratégie d’insertion adaptée.

**Sources à relire.**

- [src/engines/cursiv/ll/runtime/LlEpisode.hpp:380](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/ll/runtime/LlEpisode.hpp#L380) — erase(begin) à chaque visite
- [src/engines/cursiv/ll/runtime/LlEpisode.hpp:493](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/ll/runtime/LlEpisode.hpp#L493) — insert dans le vecteur

**Contexte LL-032 — Gestion et fusion du front de positions d’épisode.** La clef du front distingue position, niveau/profondeur d’indentation, token physique précédent et lull. Les chemins arrivant à une même clef sont marqués mixed puis classés avant déduplication. Les groupes ayant les mêmes états et tags sont comparés exactement après tri. L’ordre du front repose toutefois sur un vecteur déplacé à chaque insertion et retrait.

**Limite de preuve de ce périmètre.** Coût structurel conditionné à la largeur du front ; aucune mesure de benchmark n’a été produite.

<a id="finding-ll-memo-volume"></a>

### LL-MEMO-VOLUME — La borne de 16 candidats ne borne ni le stockage ni les octets comparés

**P3 · risque · confiance forte.** Périmètres : LL-033.

**Mécanisme identifié.** find_at limite le nombre de rows examinées, tandis que add ajoute toutes les rows/plans et que recall compare k.length octets pour chaque candidat. Aucun quota de volume n’est appliqué pendant le parse.

**Condition à vérifier.** Long document produisant beaucoup de questions distinctes ou des réponses inspectant de longs suffixes.

**Impact.** Croissance du mémo pendant le document ; un petit nombre de candidats peut encore représenter de longues comparaisons. Le chiffre 16 ne constitue pas une borne mémoire/temps globale.

**Direction de correction.** Borner le volume des plans et des octets mémorisés/comparés, ou exposer les métriques nécessaires au dimensionnement.

**Sources à relire.**

- [src/engines/cursiv/ll/runtime/LlEpisodeMemo.hpp:86](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/ll/runtime/LlEpisodeMemo.hpp#L86) — append non borné
- [src/engines/cursiv/ll/runtime/LlEpisodeMemo.hpp:126](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/ll/runtime/LlEpisodeMemo.hpp#L126) — borne sur candidats seulement
- [src/engines/cursiv/ll/runtime/LlEpisode.hpp:441](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/ll/runtime/LlEpisode.hpp#L441) — comparaison de toute la dépendance

**Contexte LL-033 — Clés, égalité et rétention du mémo d’épisode.** Le mémo compare terminal, longueur du premier token, gap, section, contexte et tous les octets inspectés. Les réponses dépendant d’EOF exigent le même espace restant. Il est désactivé avec indentation et lorsque des ties spécifiques sont passés. Les plans sont stockés avec offsets relatifs ; un digest seul n’autorise jamais une réponse.

**Limite de preuve de ce périmètre.** Revue statique ; aucune exécution.

<a id="finding-ll-op-alias-croissance"></a>

### LL-OP-ALIAS-CROISSANCE — Résolution d’une chaîne d’alias d’opérateur quadratique et récursive

**P3 · risque · confiance forte.** Périmètres : LL-005.

**Mécanisme identifié.** lay_operator recherche chaque règle par ranges::count dans reading puis appelle récursivement la résolution de son corps.

**Condition à vérifier.** Chaîne de h alias admissibles conduisant à un opérateur atomique.

**Impact.** Θ(h²) comparaisons et Θ(h) pile native avant émission ; une grammaire profonde peut épuiser la pile.

**Direction de correction.** Utiliser un ensemble d’appartenance actif et une pile explicite de frames.

**Sources à relire.**

- [src/engines/cursiv/ll/gen/LlLowerer.cpp:65](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/ll/gen/LlLowerer.cpp#L65) — test linéaire et appel récursif

**Contexte LL-005 — Abaissement des opérateurs et niveaux de précédence.** Les conversions des alias sont conservées et les conditions avant/après opérateur entourent le token atomique. Le nombre de niveaux est contrôlé avant réduction en uint8. La résolution des alias reste cependant récursive et recherche chaque règle dans toute la pile active.

**Limite de preuve de ce périmètre.** Revue statique ; aucun test ni génération exécuté.

<a id="finding-ll-productivite-eof-indirect"></a>

### LL-PRODUCTIVITE-EOF-INDIRECT — Diagnostic de règle improductive incomplet après une fin indirecte

**P3 · limite · confiance forte.** Périmètres : LL-008.

**Mécanisme identifié.** Le booléen ended n’est activé que par un enfant direct endMark et ne bloque ensuite que term ou implied. Un appel productif lisant un token reste donc productif après EOF, contrairement à son corps inline.

**Condition à vérifier.** Séquence EOF puis appel non inline vers une règle consommante, ou EOF dissimulé dans un sous-groupe.

**Impact.** Une règle n’acceptant aucun texte peut échapper au refus de génération ; aucun mauvais succès de parsing n’est allégué.

**Direction de correction.** Propager une forme abstraite distinguant les dérivations après EOF, ou documenter précisément le caractère partiel du diagnostic.

**Sources à relire.**

- [src/engines/cursiv/ll/gen/ll_build.cpp:394](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/ll/gen/ll_build.cpp#L394) — ended limité aux enfants directs
- [src/engines/cursiv/shared/gen/Lowerer.hpp:337](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/shared/gen/Lowerer.hpp#L337) — les références non inline conservent un appel

**Contexte LL-008 — Atteignabilité, productivité et refus de grammaire.** L’atteignabilité inclut primary et opérateurs ; les terminaux trivia sont refusés dans les règles vivantes. Les appels sans progrès sont collectés itérativement puis les cycles sont diagnostiqués par SCC. Le filtre de productivité est monotone mais sa connaissance d’une fin forcée ne traverse pas les appels/groupes.

**Limite de preuve de ce périmètre.** Revue statique ; aucun test ni génération exécuté.

<a id="finding-ll-return-suffix-rescan"></a>

### LL-RETURN-SUFFIX-RESCAN — Les classes de retour reparcourent les suffixes epsilon communs

**P3 · risque · confiance forte.** Périmètres : LL-010.

**Mécanisme identifié.** Chaque état de retour redémarre son exploration epsilon sans réutiliser la fermeture d’un suffixe déjà visité par un autre retour.

**Condition à vérifier.** Séquence de nombreux appels optionnels dont les retours partagent des suffixes optionnels croissants.

**Impact.** Θ(R × taille du suffixe) visites, quadratiques pour une chaîne appropriée, malgré des classes finales identiques ou proches.

**Direction de correction.** Mémoriser les fermetures des SCC epsilon ou partager le calcul des ensembles d’arrêts.

**Sources à relire.**

- [src/engines/cursiv/ll/gen/ll_build.cpp:1370](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/ll/gen/ll_build.cpp#L1370) — exploration par retour

**Contexte LL-010 — Classes de retour et états après une position.** Les classes de retour sont déterminées par un vecteur trié exact des arrêts atteints par epsilon. Le bit tail ne s’applique qu’à un unique ruleStop. La prédiction peut lire ce quotient ; track_calls rétablit les retours exacts pour la réparation, et le caret conserve aussi les sites. nodeAfter utilise nodeFollow pour les appels.

**Limite de preuve de ce périmètre.** Revue statique ; aucun test ni génération exécuté.

<a id="finding-lr-caret-caches-lineaires"></a>

### LR-CARET-CACHES-LINEAIRES — Plusieurs caches et déduplications de complétion sont parcourus linéairement

**P3 · limite · confiance forte.** Périmètres : LR-064, LR-066.

**Mécanisme identifié.** lex rescane tous les Scanned ; trivia_to rescane Crossed ; les tableaux Allowed, Glue, emitted et les features du terminal sont aussi cherchés par find. Le coût total inclut les sommes des tailles parcourues et peut être quadratique quand le nombre de modes, offsets ou états candidats grandit. Les caches sont locaux et la réutilisation est exacte.

**Condition à vérifier.** Nombreuses combinaisons de modes/positions pendant une fermeture, ou nombreuses landings d’un même terminal.

**Impact.** Latence accrue malgré la mémorisation ; pas de corruption de cache démontrée.

**Direction de correction.** Passer à un index exact au-delà d’un seuil de petite taille, et mesurer séparément le nombre de probes et de comparaisons.

**Sources à relire.**

- [src/engines/cursiv/lr/runtime/LrCaretTable.cpp:1492](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/LrCaretTable.cpp#L1492) — Recherche linéaire Scanned.
- [src/engines/cursiv/lr/runtime/LrCaretTable.cpp:1629](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/LrCaretTable.cpp#L1629) — Recherche linéaire Crossed.
- [src/engines/cursiv/lr/runtime/LrCaretTable.cpp:605](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/LrCaretTable.cpp#L605) — Recherche Allowed.
- [src/engines/cursiv/lr/runtime/LrCaretTable.cpp:2425](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/LrCaretTable.cpp#L2425) — Recherche Glue.
- [src/engines/cursiv/lr/runtime/LrCaretTable.cpp:2445](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/LrCaretTable.cpp#L2445) — Dédup des landings puis des features.

**Contexte LR-064 — Lecture lexicale autour du caret.** Les scans partagent une clé mode/offset/état inside ; les lectures dépendantes de la pile sont évaluées ensuite et ne contaminent pas cette clé. trivia_to ne mémorise son résultat que si aucun refus contextuel n’a été demandé. Les scans sont oubliés aux changements de progression ; les résultats temporaires sont copiés avant les opérations susceptibles de changer les pools. Les boucles lexicales vérifient une avancée positive et les trivia partiels passent par ends_before_word. L’exactitude locale est cohérente, avec une limite de recherche linéaire des caches.

**Limite de preuve de ce périmètre.** Analyse statique uniquement ; aucune requête de complétion ni grammaire de démonstration exécutée. Les coûts décrits sont structurels, pas mesurés.

**Contexte LR-066 — Essai de candidats et projection des features.** Les candidats passent par leurs spellings, l’élection lexicale, les guards/gaps, le maintien de la lecture précédente et les passes de réduction. Une feature est filtrée par sa landing et dédupliquée dans le groupe du terminal ; les différents sites/types ne sont pas écrasés. Les essais restaurent les piles par rollback. Les conditions peuvent être interrogées sur une valeur inconnue puis raffinées par échantillons, avec la limite d’exhaustivité décrite. Les coûts des dédups sont consolidés sous LR-CARET-CACHES-LINEAIRES et l’annulation sous CORE-COMPLETION-BOUNDS.

**Limite de preuve de ce périmètre.** Analyse statique uniquement ; aucune requête de complétion ni grammaire de démonstration exécutée. Les coûts décrits sont structurels, pas mesurés.

Comparaison incluse : [LR-066](#comparison-lr-066).

<a id="finding-lr-group-front-rescans"></a>

### LR-GROUP-FRONT-RESCANS — Le partitionnement des fonctions rescane un front potentiellement large à chaque placement

**P3 · limite · confiance forte.** Périmètres : LR-036.

**Mécanisme identifié.** grow_groups parcourt tout front pour sélectionner chaque sommet ; les sommets déjà placés restent dans ce vecteur jusqu’à la fin du groupe. Après le tri des seeds et la construction des arcs, le coût inclut la somme des tailles de ces fronts, et peut devenir quadratique lorsque beaucoup de sommets restent sur un front large. Les strings de fragments, bodies, unités et fichiers ont aussi des durées de vie qui se recouvrent.

**Condition à vérifier.** Génération de plusieurs groupes pour une grammaire dont les états/réductions ont des fronts larges ; ngroups=1 court-circuite grow_groups.

**Impact.** Temps de génération accru et pression mémoire de source temporaire ; aucun ralentissement ni RSS n’a été mesuré.

**Direction de correction.** Maintenir un ensemble de candidats priorisé avec mise à jour des gains, ou compacter les entrées placées ; déplacer les chaînes finales quand le fragment source n’est plus requis.

**Sources à relire.**

- [src/engines/cursiv/lr/gen/lr_emit_parser.cpp:2396](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/gen/lr_emit_parser.cpp#L2396) — Tri initial des graines.
- [src/engines/cursiv/lr/gen/lr_emit_parser.cpp:2416](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/gen/lr_emit_parser.cpp#L2416) — Rescan de front pour chaque sommet.
- [src/engines/cursiv/lr/gen/lr_emit_parser.cpp:2430](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/gen/lr_emit_parser.cpp#L2430) — Purge seulement à la fin du groupe.
- [src/engines/cursiv/lr/gen/lr_emit_parser.cpp:2464](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/gen/lr_emit_parser.cpp#L2464) — Bypass pour un seul groupe.

**Contexte LR-036 — Fragments, groupes et unités de compilation.** Les map de string_view pointent vers des vecteurs préalloués dont les textes antérieurs ne sont plus réécrits. enter collecte les entrées intergroupes avant les signatures, et kEnter encode groupe/local sur deux champs de 16 bits dans les domaines bornés du générateur. Les appels intergroupes imposent CURSIV_MUSTTAIL ; on ne peut donc pas déduire une croissance de pile par appels ordinaires sur le chemin supporté. Le namespace et les inline constexpr partagés assurent le raccord des unités. Deux problèmes localisés subsistent : option groupLines=0 non validée et rescans du front de partitionnement.

**Limite de preuve de ce périmètre.** Analyse statique du snapshot ; aucun programme généré compilé ni exécuté, aucune mesure de temps ou de mémoire.

<a id="finding-lsp-format-comment-idempotence"></a>

### LSP-FORMAT-COMMENT-IDEMPOTENCE — Un espace après le marqueur de commentaire peut imposer deux formatages successifs

**P3 · défaut · confiance forte.** Périmètres : EXA-007, EXA-014, LSP-037.

**Mécanisme identifié.** Le premier fragment du corps multiligne est conservé brut, contrairement aux suivants qui passent par strip_line_prefix. La suppression des lignes vides intervient avant normalize_line ; une première ligne contenant uniquement un espace reste donc comme Text vide après normalisation et est réémise en ligne préfixée. Au second passage elle devient une ligne vide supprimable.

**Condition à vérifier.** Commentaire /** suivi d’un espace puis saut de ligne, puis « * Texte », puis « */ », avec les options par défaut. Le premier format ajoute une ligne « * » vide ; le suivant l’enlève.

**Impact.** Formatting non idempotent : une deuxième application modifie encore un document que le premier passage venait de produire.

**Direction de correction.** Normaliser/trim la première ligne avant le calcul des bords vides et avant classification, puis vérifier la stabilité sur les espaces du marqueur initial.

**Sources à relire.**

- [src/lsp/cursiv/formatting/FormattingRules.cpp:251](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/formatting/FormattingRules.cpp#L251) — Première ligne conservée sans strip.
- [src/lsp/cursiv/formatting/FormattingRules.cpp:259](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/formatting/FormattingRules.cpp#L259) — Bords vides retirés avant normalisation.
- [src/lsp/cursiv/formatting/FormattingRules.cpp:276](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/formatting/FormattingRules.cpp#L276) — Normalisation tardive.
- [src/lsp/cursiv/formatting/FormattingRules.cpp:283](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/formatting/FormattingRules.cpp#L283) — Text vide néanmoins conservé.
- [src/lsp/cursiv/formatting/FormattingRules.cpp:297](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/formatting/FormattingRules.cpp#L297) — Ligne vide préfixée réémise.

**Contexte EXA-007 — Mise en forme des expressions, appels et commentaires.** Le formatter impose espaces autour de l’opérateur via sa propriété AST, resserre parenthèses/virgules et protège les branches binaires ou groupées incomplètes. Les listes vides ne contiennent grammaticalement pas de parenthèses ; leur absence de règle spécifique est donc cohérente. Chaque statement commence une ligne ; la valeur après : peut rester sur la ligne ou être indentée. Seuls les commentaires multilignes sont remplacés, via le helper commun. Aucun changement de token métier n’apparaît dans les règles propres à l’exemple. Relecture transversale : les sélecteurs de mots-clés de ces règles dépendent de la casse écrite ; voir LSP-FORMAT-KEYWORD-CASSE (LSP-035). Le helper multiligne utilisé pour les commentaires /** est aussi concerné par LSP-FORMAT-COMMENT-IDEMPOTENCE.

**Limite de preuve de ce périmètre.** Idempotence globale et résolution des conflits d’édition dépendent du moteur de formatting commun ; aucune exécution.

**Contexte EXA-014 — Mise en forme des blocs et commentaires de documentation.** Les déclarations racines perdent leur indentation ; chaque package/entité utilise le même helper de bloc. extends ne reçoit une règle que si sa référence est écrite, many seulement si le flag est présent, et le deux-points est resserré avant/espacé après. Les commentaires /** sont distingués de /* ; les marqueurs et préfixes de ligne des commentaires de documentation sont transmis explicitement, avec reconnaissance de tags @. Les line comments ont leur helper dédié. Aucun défaut local de sélection ou de conservation des marqueurs trouvé. Relecture transversale : les sélecteurs de mots-clés de ces règles dépendent de la casse écrite ; voir LSP-FORMAT-KEYWORD-CASSE (LSP-035). Le helper multiligne utilisé pour les commentaires /** est aussi concerné par LSP-FORMAT-COMMENT-IDEMPOTENCE.

**Limite de preuve de ce périmètre.** Stabilité du helper de blocs/commentaires et arbitrage des edits relèvent du moteur commun ; pas d’exécution.

**Contexte LSP-037 — Nœuds masqués et commentaires.** Les tokens hidden sont trouvés par leur règle de terminal et les callbacks par une clé string propriétaire. replace ne stocke rien si le texte ne change pas ; plusieurs remplacements gardent le dernier. Les callbacks peuvent agrandir le pool d’actions : le moteur relit les pointeurs ensuite et le pending append conserve une clé, évitant des pointeurs de vector invalidés. La réécriture du commentaire est séparée des marges et incorpore la marge dans le remplacement si les offsets coïncident ; sans remplacement, chaque ligne reçoit seulement un edit de marge. Les helpers conservent marqueurs, style de ligne, plafond de lignes vides et structure de tags selon les options. Deux limites ressortent : les actions d’espacement prepend ne sont pas exécutées sur un hidden et une première ligne de corps blanche rend le helper multiligne non idempotent.

**Limite de preuve de ce périmètre.** Lecture statique uniquement ; aucune exécution du projet ni mesure de coût.

Comparaison incluse : [LSP-037](#comparison-lsp-037).

<a id="finding-lsp-semantic-delta-extension"></a>

### LSP-SEMANTIC-DELTA-EXTENSION — L’API ne permet pas à une langue d’activer les semantic tokens delta

**P3 · écart d’API · confiance forte.** Périmètres : LSP-040.

**Mécanisme identifié.** Le contrat Cursiv possède full et range seulement ; le serveur annonce full=true sans delta et il n’existe pas de méthode de provider recevant previousResultId. Langium expose semanticHighlightDelta ainsi qu’une option delta redéfinissable et un builder conservé par document.

**Condition à vérifier.** Une langue veut optimiser les rafraîchissements de coloration avec des deltas ou porter un provider Langium ayant activé cette option.

**Impact.** Extension impossible par remplacement du seul provider ; les réponses complètes doivent être renvoyées et le routage serveur modifié pour ajouter delta.

**Direction de correction.** Ajouter une capacité optionnelle et la méthode delta avec résultat/identifiant de version, sans l’activer tant que le provider ne la prend pas en charge.

**Sources à relire.**

- [src/lsp/cursiv/semantic/SemanticTokenProvider.hpp:34](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/semantic/SemanticTokenProvider.hpp#L34) — Surface limitée àfull/range.
- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:355](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L355) — Capacité full sans delta.
- [langium/packages/langium/src/lsp/semantic-token-provider.ts:77](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/semantic-token-provider.ts#L77) — Méthode delta Langium.
- [langium/packages/langium/src/lsp/semantic-token-provider.ts:276](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/semantic-token-provider.ts#L276) — Delta désactivé par défaut mais redéfinissable.
- [langium/packages/langium/src/lsp/semantic-token-provider.ts:303](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/semantic-token-provider.ts#L303) — previousResultId puis buildEdits.

**Contexte LSP-040 — Encodage et découpage des spans.** encode émet exactement5entiers, avec delta de ligne puis colonne absolue après changement de ligne ou relative sur la même ligne ; les longueurs nulles sont ignorées. Le tri et la borne emitted imposent l’ordre sans chevauchement avant le Cursor. Celui-ci convertit progressivement les offsets source en UTF-16, saute les fins de ligne et traite CRLF comme une terminaison ; emit découpe chaque span multiligne avant ces terminateurs. Range retient tout token qui touche la fenêtre, sans couper un token à la borne, conformément à son contrat ; les feuilles utilisent un index de fins mais noms/références sont toujours collectés sur le document. Les deux routes attendent Linked. Aucun défaut d’ordre, de longueur ou d’indice de légende n’est démontré sur ces chemins ; la lacune concerne l’extension delta, et non une option active par défaut dans Langium.

**Limite de preuve de ce périmètre.** Lecture statique uniquement ; aucune exécution du projet ni mesure de coût.

Comparaison incluse : [LSP-040](#comparison-lsp-040).

<a id="finding-ws-cac-bootstrap-noexcept"></a>

### WS-CAC-BOOTSTRAP-NOEXCEPT — Une allocation initiale du gestionnaire d’epochs ne peut pas remonter comme bad_alloc

**P3 · écart d’API · confiance forte.** Périmètres : CAC-006.

**Mécanisme identifié.** state() fait new State au premier appel, alors que Guard() est noexcept. Un refus de cette allocation passe donc par std::terminate, contrairement aux allocations ordinaires de table qui peuvent remonter.

**Condition à vérifier.** Première opération gardée d’une table dans un processus soumis à un échec d’allocation du State global.

**Impact.** Le client ne peut récupérer cette panne par catch bad_alloc autour de l’accès au cache.

**Direction de correction.** Préinitialiser ce gestionnaire dans une opération pouvant échouer, ou documenter explicitement cette allocation fatale et éviter un noexcept trompeur.

**Sources à relire.**

- [src/core/cursiv/utils/ConcurrentTable.cpp:65](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/utils/ConcurrentTable.cpp#L65) — Allocation initiale du State.
- [src/core/cursiv/utils/ConcurrentTable.hpp:47](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/utils/ConcurrentTable.hpp#L47) — Guard noexcept appelle takeSlot.

**Contexte CAC-006 — Enregistrement des lecteurs par epoch.** Le guard extérieur enregistre l’epoch et les guards imbriqués incrémentent seulement depth. À sa sortie, le slot devient idle ; taken n’est rendu qu’au destructeur TLS, ou immédiatement pour les accès effectués après ce destructeur. Les slots sont globaux à toutes les tables et leur tableau a 1024 entrées. takeSlot ne fournit ni croissance ni issue d’échec : un thread supplémentaire boucle avec yield même si les détenteurs ne lisent plus rien. Le premier accès au State alloue aussi dans une chaîne Guard noexcept.

**Limite de preuve de ce périmètre.** Conséquences de contrôle établies par lecture ; aucune saturation de threads ni panne d’allocation provoquée.

<a id="finding-ws-cac-document-partitions"></a>

### WS-CAC-DOCUMENT-PARTITIONS — Le cache conserve une partition pour chaque identité documentaire rencontrée

**P3 · limite · confiance forte.** Périmètres : CAC-012.

**Mécanisme identifié.** Supprimer un document vide sa table mais ne retire ni Values, ni son entrée de _documents. La série des DocumentId consultés fait donc croître le nombre de partitions même si le workspace vivant reste petit.

**Condition à vérifier.** Hôte de longue durée parcourant de nombreux documents distincts ou temporaires et lisant ces documents par DocumentCache.

**Impact.** Rétention de métadonnées et de tables minimales proportionnelle à l’historique, pas au nombre de documents vivants.

**Direction de correction.** Documenter cette borne d’historique ; si nécessaire, fournir une purge sûre ou une indirection/génération permettant de rendre les partitions sans invalider les accès TLS actifs.

**Sources à relire.**

- [src/core/cursiv/utils/DocumentCache.hpp:96](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/utils/DocumentCache.hpp#L96) — Suppression : clear(false), pas retrait.
- [src/core/cursiv/utils/DocumentCache.hpp:131](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/utils/DocumentCache.hpp#L131) — Création cumulative dans _owned.
- [src/core/cursiv/utils/DocumentCache.hpp:153](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/utils/DocumentCache.hpp#L153) — Conservation explicitement annoncée.

**Contexte CAC-012 — Partitionnement documentaire et accès TLS.** valuesOf utilise un cache TLS indexé par serial d’instance et DocumentId ; un changement d’adresse/instance ne suffit donc pas à réutiliser un ancien pointeur. Les tables Values sont dans une deque et ne sont pas supprimées, ce qui rend ces pointeurs stables. find_or_create refait le lookup sous _creating avant emplacement et publication. Si la publication lève, la table supplémentaire reste possédée mais inutilisée ; aucun dangling n’est produit. Cette stabilité est achetée par la conservation d’une partition par identité jamais lue.

**Limite de preuve de ce périmètre.** Limite volontaire de stabilité mémoire ; aucun volume réel ni fuite d’objets hors de leur propriétaire n’est affirmé.

<a id="finding-ws-cac-snapshot-retention"></a>

### WS-CAC-SNAPSHOT-RETENTION — Le dernier snapshot retiré peut rester détenu après la sortie de tous ses lecteurs

**P3 · limite · confiance forte.** Périmètres : CAC-010.

**Mécanisme identifié.** Si le dernier publish observe une stripe active, le snapshot demeure dans retired ; la sortie ultérieure du lecteur décrémente seulement active. Il n’existe pas de collecte publique indépendante d’une nouvelle publication.

**Condition à vérifier.** Invalidation finale d’un gros snapshot alors qu’un lecteur est actif, puis absence de nouvelles publications pendant une longue période.

**Impact.** Rétention jusqu’à une publication supplémentaire ou destruction du service ; une succession de lecteurs partageant une stripe peut prolonger cette rétention.

**Direction de correction.** Prévoir un drain explicite après quiescence si nécessaire et documenter cette politique ; préserver le handshake et l’absence de réclamation prématurée.

**Sources à relire.**

- [src/core/cursiv/utils/Published.hpp:55](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/utils/Published.hpp#L55) — Sortie sans collecte.
- [src/core/cursiv/utils/Published.hpp:91](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/utils/Published.hpp#L91) — Collecte appelée seulement depuis publish.
- [src/core/cursiv/utils/Published.hpp:123](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/utils/Published.hpp#L123) — Retenue tant qu’un bit owed subsiste.

**Contexte CAC-010 — Publication et retrait des snapshots.** publish copie d’abord le propriétaire live dans retired ; si cette allocation lève, le pointeur publié et son owner restent inchangés. Il remplace ensuite live, publie le pointeur en seq_cst et retire progressivement les bits owed de chaque snapshot selon les stripes devenues idle. Les stripes n’ont pas à être idle simultanément. La destruction d’un Read ne collecte cependant rien ; seules les publications ultérieures ou la destruction de Published rendent le dernier lot retenu récupérable.

**Limite de preuve de ce périmètre.** Pas de défaut de durée de vie confirmé. Destructeurs de T sous _writers : sûrs pour Exports/Incoming examinés, aucune garantie générique de réentrance.

<a id="finding-ws-lsp-navigation-null-erased"></a>

### WS-LSP-NAVIGATION-NULL-ERASED — Le serveur efface la distinction nullopt/liste vide des providers de navigation

**P3 · écart d’API · confiance forte.** Périmètres : LSP-042.

**Mécanisme identifié.** DeclarationProvider, TypeDefinitionProvider et ImplementationProvider documentent une différence entre nullopt et une liste vide. Leurs wrappers DefaultLanguageServer renvoient tous un vector initialement vide ; nullopt et [] deviennent donc la même réponse tableau par navigation_result.

**Condition à vérifier.** Un provider cherche à conserver cette distinction promise par son interface.

**Impact.** Le contrat public ne correspond pas à la valeur transmise au client, même si ces deux formes signifient couramment aucune destination.

**Direction de correction.** Conserver l’optional jusqu’au runtime, ou retirer cette promesse et simplifier les interfaces.

**Sources à relire.**

- [src/lsp/cursiv/navigation/DeclarationProvider.hpp:20](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/navigation/DeclarationProvider.hpp#L20) — Promesse de distinction.
- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:955](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L955) — Vecteur par défaut.
- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:963](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L963) — Retour non optionnel.
- [src/lsp/cursiv/runtime/LanguageServerRuntime.cpp:372](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/LanguageServerRuntime.cpp#L372) — Encodage systématique du vecteur.

**Contexte LSP-042 — Provider de déclaration.** DeclarationProvider est un slot distinct, optionnel, annoncé seulement s’il est installé. Le routage attend Linked et transmet document/position/token ; la variante de LocationLink est adaptée aux capacités client. La nullabilité annoncée par l’interface du provider est cependant effacée par le wrapper serveur.

**Limite de preuve de ce périmètre.** Audit statique uniquement ; la logique métier des providers optionnels dépend du langage qui les installe.

Comparaison incluse : [LSP-042](#comparison-lsp-042).

<a id="finding-ws-lsp-plaintext-markup"></a>

### WS-LSP-PLAINTEXT-MARKUP — L’adaptation en texte brut laisse les marqueurs Markdown visibles

**P3 · limite · confiance forte.** Périmètres : LSP-034.

**Mécanisme identifié.** as_plain_text remplace seulement MarkupKind::Markdown par PlainText ; la valeur garde liens, backticks et autres marqueurs produits par DocumentationProvider. La même adaptation s’applique à hover et à la documentation de complétion.

**Condition à vérifier.** Client annonçant plaintext avant markdown ou n’annonçant aucun format, avec documentation Markdown.

**Impact.** Texte moins lisible et liens présentés littéralement ; respect du type transporté mais rendu dégradé.

**Direction de correction.** Prévoir un rendu texte brut des documentations ou une transformation conservatrice avant de changer kind.

**Sources à relire.**

- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:182](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L182) — Changement du kind seulement.
- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:1120](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L1120) — Usage hover.
- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:1135](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L1135) — Usage completion.

**Contexte LSP-034 — Survol.** Le hover cherche d’abord les déclarations dénotées puis leurs documentations dans leur langue d’origine ; il rejoint les textes multi-cibles avec un espace. À défaut, une documentation grammaticale du token est recherchée. La plage reste celle du nom source. Le serveur adapte le kind aux préférences client, mais ne transforme pas réellement la syntaxe Markdown lorsque le client préfère du texte brut.

**Limite de preuve de ce périmètre.** Lecture statique de l’implémentation, des contrats et des callers ; aucun test ou code projet exécuté.

Comparaison incluse : [LSP-034](#comparison-lsp-034).

<a id="finding-ws-mcp-document-source-label"></a>

### WS-MCP-DOCUMENT-SOURCE-LABEL — L’inventaire annonce sur disque tout document fermé, même virtuel

**P3 · défaut · confiance forte.** Périmètres : MCP-008.

**Mécanisme identifié.** documents écrit 'open' si isOpen et 'on disk' sinon. isOpen décrit seulement la présence d’un buffer client ; un document de bibliothèque virtuel ou créé par fromString peut être fermé et ne correspondre à aucun fichier.

**Condition à vérifier.** Workspace possédant un document virtuel/préchargé ou un scratch MCP visible pendant validation.

**Impact.** Le consommateur peut croire pouvoir lire ou modifier un fichier inexistant sur disque.

**Direction de correction.** Afficher 'closed' ou une origine explicitement connue ; ne déduire la persistance que d’un contrat de contenu.

**Sources à relire.**

- [src/mcp/cursiv/DefaultMcpService.cpp:801](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/mcp/cursiv/DefaultMcpService.cpp#L801) — Étiquette déduite de isOpen.
- [src/core/cursiv/workspace/Document.hpp:64](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/Document.hpp#L64) — isOpen décrit seulement la vue client.

**Contexte MCP-008 — Documents détenus et contenu courant.** documents trie et copie les métadonnées sous read ; text-document-content copie le texte courant, y compris les buffers d’éditeur, sans recharger le disque ni muter le store. URI inconnue et document vide restent distingués par found. L’étiquette 'on disk' est toutefois déduite seulement de !isOpen, ce qui ne décrit pas les documents virtuels ou temporaires fermés.

**Limite de preuve de ce périmètre.** Analyse statique uniquement ; aucun nouvel outil MCP du projet exécuté pour cette fiche. Langium ne figure pas comme comparaison prévue pour ce périmètre.

<a id="finding-ws-mcp-outline-depth"></a>

### WS-MCP-OUTLINE-DEPTH — Le plan MCP est tronqué silencieusement à 17 niveaux

**P3 · limite · confiance forte.** Périmètres : MCP-010.

**Mécanisme identifié.** walk retourne immédiatement pour depth>16 sans ajouter d’indication à la réponse. La description de l’outil ne donne pas cette borne.

**Condition à vérifier.** Déclarations nommées imbriquées au-delà de 17 niveaux.

**Impact.** Le consommateur peut considérer le plan incomplet comme exhaustif.

**Direction de correction.** Rendre explicite la limite et afficher un marqueur d’élision, ou passer à un parcours itératif avec une borne de taille globale.

**Sources à relire.**

- [src/mcp/cursiv/DefaultMcpService.cpp:882](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/mcp/cursiv/DefaultMcpService.cpp#L882) — Borne silencieuse.
- [src/mcp/cursiv/DefaultMcpService.cpp:865](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/mcp/cursiv/DefaultMcpService.cpp#L865) — Description sans limite.

**Contexte MCP-010 — Plan d’un document.** Le plan MCP reconstruit une hiérarchie depuis LocalSymbols, commence à Document puis essaie la racine si rien n’est trouvé. Les descendants ne sont visités qu’à travers une entrée de symbole existante ; cette stratégie ne traverse pas les conteneurs AST anonymes et diffère de collect_named_nodes du provider LSP. La profondeur supérieure à 16 est aussi coupée sans marqueur.

**Limite de preuve de ce périmètre.** Analyse statique uniquement ; aucun nouvel outil MCP du projet exécuté pour cette fiche. Langium ne figure pas comme comparaison prévue pour ce périmètre.

<a id="finding-ws-sch-external-convoy"></a>

### WS-SCH-EXTERNAL-CONVOY — Les demandeurs externes convergent sur les mêmes premiers points de contention

**P3 · limite · confiance forte.** Périmètres : SCH-006.

**Mécanisme identifié.** Les submitters externes commencent tous le scan de cellules à zéro, la publication au bucket zéro et le vol coopératif à self=workers.size(). Les buckets répartissent les parts d’un job mais ne dispersent pas le début des jobs concurrents.

**Condition à vérifier.** Plusieurs threads clients utilisent simultanément le même scheduler, notamment avec de petits jobs fréquents.

**Impact.** Contention conditionnelle sur les premières cellules/queues et coût O(P) du selfIndex par run ; aucun ralentissement chiffré n’est établi.

**Direction de correction.** Varier le point de départ par demandeur ou ticket tout en préservant la distribution de parts et la propriété des deques.

**Sources à relire.**

- [src/core/cursiv/execution/WorkerPool.cpp:214](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/execution/WorkerPool.cpp#L214) — Boucle de publication démarrant à at=0.
- [src/core/cursiv/execution/WorkerPool.cpp:244](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/execution/WorkerPool.cpp#L244) — Offset self identique pour tous les externes.
- [src/core/cursiv/execution/WorkerPool.cpp:312](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/execution/WorkerPool.cpp#L312) — Premier CAS toujours sur la première cellule.

**Contexte SCH-006 — Distribution des publications externes.** Un worker publie uniquement sur sa deque propriétaire. Les threads externes distribuent les rangs de parts sur des buckets dont le mutex sérialise les producteurs, préservant ainsi la propriété single-producer de TaskQueue. published n’est incrémenté qu’après un push terminé ; le décompte en cas d’exception reste exact. La distribution commence néanmoins toujours par le bucket zéro pour chaque caller externe, comme la recherche de cellule commence au premier slot.

**Limite de preuve de ce périmètre.** L’impact dépend de la concurrence externe ; les usages séquentiels usuels ne suffisent pas à le quantifier.

<a id="finding-ws-sch-private-slice"></a>

### WS-SCH-PRIVATE-SLICE — Une tranche réservée reste indivisible sur une charge hétérogène

**P3 · limite · confiance forte.** Périmètres : SCH-005.

**Mécanisme identifié.** La taille de tranche dépend uniquement du nombre d’indices restants et de parts, puis son propriétaire exécute tous ses indices séquentiellement. Une part terminée ailleurs ne peut reprendre le reliquat de cette tranche.

**Condition à vérifier.** Plusieurs documents ou lots coûteux sont contigus et tombent dans une même réservation guidée.

**Impact.** La durée de phase peut être dominée par une tranche longue malgré des workers devenus libres ; ce n’est pas une erreur de couverture.

**Direction de correction.** Prévoir des lots plus fins ou une politique de coût par appelant pour les charges hétérogènes ; conserver une politique guidée pour les corps uniformes.

**Sources à relire.**

- [src/core/cursiv/execution/WorkerPool.cpp:104](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/execution/WorkerPool.cpp#L104) — slice ne dépend que de left/parties.
- [src/core/cursiv/execution/WorkerPool.cpp:109](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/execution/WorkerPool.cpp#L109) — Exécution privée séquentielle de la tranche.

**Contexte SCH-005 — Réservation des tranches de travail.** Le CAS coarse réserve [first,last) en avançant next ; les autres parts reprennent le next courant après échec. Le mode fin attribue chaque indice par fetch_add. Pour les tailles réalistes représentables et parties>=1, slice<=left et last ne dépasse pas count. L’équilibrage ne redistribue toutefois jamais le reste d’une tranche déjà réservée : le tri en tête des documents ouverts n’est pas une priorité d’achèvement.

**Limite de preuve de ce périmètre.** Pas de mesure du déséquilibre ; les compteurs proches de SIZE_MAX sont hors des capacités de stockage des consommateurs examinés.

<a id="finding-ws-val-anchor-noexcept"></a>

### WS-VAL-ANCHOR-NOEXCEPT — La recherche d’une ancre indexée alloue dans une méthode noexcept

**P3 · écart d’API · confiance forte.** Périmètres : VAL-008.

**Mécanisme identifié.** anchorSpan est noexcept mais les recherches de toutes occurrences, la copie du nom et _anchors.emplace_back peuvent allouer. Une allocation défaillante provoque terminate avant le confinement des exceptions prévu pour les checks.

**Condition à vérifier.** Un check demande une ancre avec index>0 et une allocation de collecte/cache échoue.

**Impact.** Arrêt du processus, contrairement aux erreurs ordinaires de check transformées en diagnostics.

**Direction de correction.** Retirer noexcept de ce chemin et des wrappers concernés ou prévoir un fallback sans allocation.

**Sources à relire.**

- [src/core/cursiv/validation/ValidationAcceptor.cpp:82](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/validation/ValidationAcceptor.cpp#L82) — anchorSpan noexcept.
- [src/core/cursiv/validation/ValidationAcceptor.cpp:102](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/validation/ValidationAcceptor.cpp#L102) — Collecte et allocation du cache.

**Contexte VAL-008 — Construction et émission d’un diagnostic.** Le builder possède ses métadonnées, s’émet à la destruction et transmet cette responsabilité lors d’un move ; move-assignment émet d’abord le diagnostic en attente. Le sink et le document sont empruntés pour la passe. emit désarme avant l’appel du sink et normalise end>=begin ; l’émission noexcept absorbe une exception du sink, tandis que accept direct la propage. La source fournie reste préservée. Les ancres mises en cache distinguent CST et NodeId, mais l’absence d’ancre ne respecte pas le maintien de plage documenté.

**Limite de preuve de ce périmètre.** Analyse statique uniquement ; les branches d’exception et coûts structurels ne sont pas des mesures ni des reproductions.

<a id="finding-ws-val-options-optin"></a>

### WS-VAL-OPTIONS-OPTIN — Une surcharge des options de validation nécessite aussi un second opt-in

**P3 · écart d’API · confiance forte.** Périmètres : VAL-007.

**Mécanisme identifié.** Surcharger validateSingleNodeOptions ne suffit pas : asksNodeOptions doit aussi retourner true. Sinon le hook est ignoré en release ; seul le chemin séquentiel dispose d’une assertion de diagnostic en debug. Cette double obligation est documentée mais diffère de Langium et peut surprendre un portage.

**Condition à vérifier.** Un DocumentValidator dérivé adapte uniquement validateSingleNodeOptions.

**Impact.** Les nœuds censés être exclus peuvent être validés ; avec les assertions actives le petit parcours peut s’arrêter.

**Direction de correction.** Fournir une classe/helper d’options qui active automatiquement le mode ou une API unique exprimant cette capacité.

**Sources à relire.**

- [src/core/cursiv/validation/DefaultDocumentValidator.hpp:94](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/validation/DefaultDocumentValidator.hpp#L94) — Hook et opt-in séparés.
- [src/core/cursiv/validation/DefaultDocumentValidator.cpp:401](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/validation/DefaultDocumentValidator.cpp#L401) — Test unique d’asksNodeOptions.
- [src/core/cursiv/validation/DefaultDocumentValidator.cpp:442](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/validation/DefaultDocumentValidator.cpp#L442) — Assertion limitée au parcours check séquentiel.
- [langium/packages/langium/src/validation/document-validator.ts:249](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/validation/document-validator.ts#L249) — Hook toujours demandé dans Langium.

**Contexte VAL-007 — Sélection et élagage des nœuds à valider.** Quand asksNodeOptions est activé, les options sont collectées avant les checks, ce qui rend l’élagage indépendant de l’ordre enfants/parents de nodes(). validateNode=false n’exclut que le nœud ; validateChildren=false exclut ses descendants, via une mémoïsation des chaînes d’ancêtres. Le parcours reste séquentiel dans ce mode, et les ensembles/path ne sont pas partagés entre workers. Les options sont néanmoins demandées à tous les nœuds, même sous un sous-arbre qui sera écarté.

**Limite de preuve de ce périmètre.** Analyse statique uniquement ; les branches d’exception et coûts structurels ne sont pas des mesures ni des reproductions.

Comparaison incluse : [VAL-007](#comparison-val-007).

<a id="finding-ws-wsp-wait-cancelled-build"></a>

### WS-WSP-WAIT-CANCELLED-BUILD — Annuler un build ne termine pas les waiters dépourvus de leur propre annulation

**P3 · écart d’API · confiance forte.** Périmètres : WSP-026.

**Mécanisme identifié.** runAttempt relaie OperationCancelled sans publierFailure ni issue terminale. Un waiter sur un jalon non atteint peut donc rester suspendu si aucune reconstruction ne suit, même quand l’appel de build annulé est déjà terminé.

**Condition à vérifier.** Consommateur core utilisant waitUntil sans token propre, puis annulation du seul build susceptible d’atteindre son jalon, sans nouvelle tentative.

**Impact.** L’hôte doit explicitement annuler ses attentes ou produire une nouvelle publication ; la formule interne « per-document wait always terminates » exige cette hypothèse de progression externe.

**Direction de correction.** Documenter cette dépendance de liveness et relier le token des attentes au cycle de l’opération/hôte, ou exposer une issue dédiée à la tentative annulée si l’API veut cette garantie.

**Sources à relire.**

- [src/core/cursiv/workspace/DefaultDocumentBuilder.cpp:729](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultDocumentBuilder.cpp#L729) — Aucun état terminal publié pour OperationCancelled.
- [src/core/cursiv/workspace/DefaultDocumentBuilder.cpp:780](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultDocumentBuilder.cpp#L780) — Attente sur publication ou failure uniquement.
- [src/core/cursiv/workspace/DefaultDocumentBuilder.cpp:791](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultDocumentBuilder.cpp#L791) — Commentaire de terminaison documentaire à qualifier.

**Contexte WSP-026 — Attente d’une publication du workspace.** beginAttempt redescend currentState à Changed et incrémente le numéro de tentative avant le moindre callback/mutation. waitUntil global capture le compteur de publications du jalon ; il peut répondre Reached si ce jalon a été publié puis redescendu avant son réveil, sans confondre une ancienne publication antérieure à l’attente. Les exceptions ordinaires enregistrent failedAttempt et réveillent ; la comparaison par numéro ne transporte pas un échec ancien dans une nouvelle tentative. OperationCancelled ne publie aucune fin d’attente : le mécanisme compte sur une prochaine tentative ou le token du waiter.

**Limite de preuve de ce périmètre.** Il s’agit d’une obligation d’intégration, pas d’un deadlock du builder ; les waits portent contractuellement sur un jalon et non une tentative.

Comparaison incluse : [WSP-026](#comparison-wsp-026).


## Réserves à lever

Ces sept fiches ont été examinées mais ne justifient pas un correctif algorithmique sur la seule base du rapport. Elles précisent ce qui manque pour conclure.

<a id="reserve-bld-019"></a>

### BLD-019 — Production et export de couverture

Le workflow instrumente C++/liens, désactive mimalloc et distingue les profils par PID et signature binaire. La collecte fusionne tous les profils, cherche les exécutables avec sections de couverture et les fournit dans une même invocation llvm-cov ; elle exclut dépendances, code généré et sources hors src avant conversion. Le convertisseur agrège les lignes par fichier relatif et numéro avec OR de couverture, échappe les attributs XML et compte les BRDA par ligne. Build, tests et collecte précèdent et ne dépendent pas des secrets SonarCloud ; l’upload absent est expliqué explicitement.

**Élément à établir.** Analyse statique uniquement ; aucun build, test, générateur, packaging ou workflow exécuté. Réserve : le convertisseur jette l’identité block/branch des BRDA et compte les occurrences. Une entrée LCOV avec sections SF répétées d’une même branche doublerait le dénominateur, alors que les lignes sont fusionnées ; aucune preuve que l’unique llvm-cov export du workflow émette cette duplication. Les profils des builds d’intégration lancés avec leurs propres flags peuvent ne pas être instrumentés ; la couverture n’est donc pas celle de toute exécution indirecte.

**Sources.**

- [.github/workflows/sonarcloud.yml:23](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/.github/workflows/sonarcloud.yml#L23) — identité des profils
- [.github/workflows/sonarcloud.yml:50](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/.github/workflows/sonarcloud.yml#L50) — instrumentation
- [.github/workflows/sonarcloud.yml:66](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/.github/workflows/sonarcloud.yml#L66) — merge, objets et exclusions
- [tools/lcov_to_sonar.py:14](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/tools/lcov_to_sonar.py#L14) — SF/DA/BRDA
- [tools/lcov_to_sonar.py:28](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/tools/lcov_to_sonar.py#L28) — XML trié et échappé
- [.github/workflows/sonarcloud.yml:84](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/.github/workflows/sonarcloud.yml#L84) — upload facultatif

<a id="reserve-com-011"></a>

### COM-011 — Exploration des fautes de frappe dans le DFA

Le parcours est organisé par nombre de fautes : le chemin exact est traité avant les fronts fautifs, puis chaque désaccord produit suppression bornée, transposition et avances de substitution/insertion. Les fautes initiales restent ancrées à plain et les lectures de plusieurs fautes ne dépassent pas le mot ordinaire. L’émission combine terminal et frontière dans owed. La réserve concerne la domination dans first : sa clé ne conserve que position, état DFA, head et tail, alors que valid intervient dans le seuil d’émission et spelt dans le prix. Le commentaire affirme la domination du premier passage, mais la lecture locale ne démontre pas que deux chemins arrivant au même état possèdent toujours une valeur valid/spelt substituable. Aucun chemin grammatical concret invalidant cette hypothèse n’a été établi ; ce n’est donc pas un défaut confirmé.

**Élément à établir.** À lever par une preuve de dominance incluant valid/spelt ou par un cas de grammaire analysé exhaustivement. Aucune simulation ou exécution de recherche n’a été réalisée.

**Sources.**

- [src/engines/cursiv/shared/runtime/TypoWalk.hpp:183](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/shared/runtime/TypoWalk.hpp#L183) — Clé de déduplication et dominance plain/head, free/tail.
- [src/engines/cursiv/shared/runtime/TypoWalk.hpp:216](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/shared/runtime/TypoWalk.hpp#L216) — valid et spelt influencent pourtant l’admissibilité et le coût.
- [src/engines/cursiv/shared/runtime/TypoWalk.hpp:253](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/shared/runtime/TypoWalk.hpp#L253) — Les différentes transitions modifient ces compteurs différemment.
- [src/engines/cursiv/lr/runtime/lr_rescan.hpp:164](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/lr_rescan.hpp#L164) — Le noyau partagé est utilisé pour les candidats réels LR.

<a id="reserve-gra-004"></a>

### GRA-004 — Descripteurs C++ vers image de grammaire

ImageOf produit des descripteurs statiques par couple vue/type ; GrammarImage interne les règles avant de matérialiser leurs faces, ce qui coupe les cycles d’appels sans recopier leur corps indéfiniment. Les enfants d’expression sont matérialisés avant le parent et les dérivés se comparent par kind/flags/paramètres/identités/liste exacte d’enfants : un hash seul ne décide pas de l’égalité. Les noms restent empruntés aux descripteurs statiques. Chaque grammar_image retourne sa propre image mutable.

**Élément à établir.** Réserve : ready met completed avant les allocations de face ; après une exception, la réutilisation du même GrammarImage pourrait voir une face partielle. Les appels générateurs inspectés abandonnent leur image en cas d’échec. Les IDs 32 bits et la profondeur récursive d’expression ne sont pas validés globalement à cette couche.

**Sources.**

- [src/core/cursiv/grammar/ImageOf.hpp:454](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/grammar/ImageOf.hpp#L454) — Descripteur de règle et vues.
- [src/core/cursiv/grammar/GrammarImage.cpp:41](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/grammar/GrammarImage.cpp#L41) — Interning des expressions.
- [src/core/cursiv/grammar/GrammarImage.cpp:67](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/grammar/GrammarImage.cpp#L67) — Égalité exacte des dérivés.
- [src/core/cursiv/grammar/GrammarImage.cpp:105](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/grammar/GrammarImage.cpp#L105) — Règle internée avant expansion de sa face.

<a id="reserve-host-006"></a>

### HOST-006 — Threads WASI et sortie du processus Node

Les workers reçoivent le module déjà compilé et la même mémoire partagée. Le créateur refuse un ID hors ]0,2^29[ et le worker contrôle aussi cette condition avant de toucher startArg. Une sortie ou exception dans execute publie par CAS le premier code dans un mot partagé ; le superviseur attend ce mot de façon asynchrone et termine tout le processus, même si un parent C++ attend un join. stdout/stderr JS des workers sont isolés pour ne pas rendre fd1 non bloquant ; writeStderr utilise directement fd2. La fin normale d’un pthread ne publie pas de sortie de processus.

**Élément à établir.** La supervision structurée couvre execute ; les erreurs de démarrage JS avant ce try et les erreurs d’un worker descendant sans handler error dédié demanderaient une preuve supplémentaire pour affirmer qu’elles ne peuvent jamais laisser un parent bloqué. Aucune panne système injectée.

**Sources.**

- [tools/wasi-run.mjs:252](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/tools/wasi-run.mjs#L252) — Bornes des IDs et code de sortie premier gagnant.
- [tools/wasi-run.mjs:264](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/tools/wasi-run.mjs#L264) — Création de worker et isolation stdio.
- [tools/wasi-run.mjs:384](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/tools/wasi-run.mjs#L384) — Superviseur et boucle waitAsync.
- [tools/wasi-run.mjs:407](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/tools/wasi-run.mjs#L407) — Gestion des exceptions du worker.

<a id="reserve-lex-009"></a>

### LEX-009 — Produits de refus et reconnaissance incomplète

Le produit est différé jusqu’à complétude du NFA et mémorisé par niveau/continuation. Les motifs non représentables produisent un résultat incomplet explicite (boucle, lecture courte, relais, lecture au-delà). Les retries construisent sur une copie du NFA et font croître monotonement les guards pris, en restaurant les pools ajoutés. Les contrôles trivia/section incomplets sont dédupliqués et documentent un parseur généré sous hypothèse de non-recouvrement.

**Élément à établir.** Réserve de ressources : total parcourt un ensemble de sous-ensembles sans quota local, avant le plafond DFA. Une famille de grammaires admissibles provoquant une explosion de ce parcours n’a pas été établie ; ce point n’est pas présenté comme défaut démontré.

**Sources.**

- [src/engines/cursiv/shared/gen/LexRefusalProduct.hpp:64](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/shared/gen/LexRefusalProduct.hpp#L64) — Produits différés et cache.
- [src/engines/cursiv/shared/gen/LexRefusalProduct.hpp:134](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/shared/gen/LexRefusalProduct.hpp#L134) — Exploration de sous-ensembles dans total.
- [src/engines/cursiv/shared/gen/LexRefusalProduct.hpp:236](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/shared/gen/LexRefusalProduct.hpp#L236) — Motifs refusés ou incomplets.
- [src/engines/cursiv/shared/gen/LexRefusalProduct.hpp:376](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/shared/gen/LexRefusalProduct.hpp#L376) — Retries monotones et restauration des pools.
- [src/engines/cursiv/shared/gen/LexCheckIncomplete.hpp:15](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/shared/gen/LexCheckIncomplete.hpp#L15) — Signalement des comparaisons non établies.

<a id="reserve-ll-029"></a>

### LL-029 — Historiques persistants et relation de préférence

Les historiques utilisent arbres AVL de runs et queue de huit événements ; les opérations de concaténation contrôlent les longueurs et la comparaison après digest est exacte. Les greffes conservent les suffixes et la comparaison de préfixes saute les sous-arbres identiques. La préférence before ignore at/lull comme départage au premier événement différent : sa validité comme ordre strict faible dépend donc d’un invariant des histoires effectivement comparées.

**Élément à établir.** Réserve : sur histoires arbitraires, A=[(word1,at0),(word2,at1)], B=[(word1,at2),(word9,at3)], C=[(word1,at0),(word3,at1)] donnent A~B et B~C mais A<C. L’atteignabilité simultanée de telles histoires dans un tri réel n’est pas établie ; ne pas qualifier ce point de bug runtime.

**Sources.**

- [src/engines/cursiv/ll/runtime/LlHistory.hpp:56](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/ll/runtime/LlHistory.hpp#L56) — append borné
- [src/engines/cursiv/ll/runtime/LlHistory.hpp:112](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/ll/runtime/LlHistory.hpp#L112) — ordre fondé seulement sur word
- [src/engines/cursiv/ll/runtime/LlHistory.hpp:340](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/ll/runtime/LlHistory.hpp#L340) — interning exact
- [src/engines/cursiv/ll/runtime/LlHistory.hpp:450](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/ll/runtime/LlHistory.hpp#L450) — équilibrage
- [src/engines/cursiv/ll/runtime/LlHistory.hpp:564](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/ll/runtime/LlHistory.hpp#L564) — comparaison exacte des arbres
- [src/engines/cursiv/ll/runtime/LlEpisodeSim.hpp:609](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/ll/runtime/LlEpisodeSim.hpp#L609) — utilisation dans stable_sort

<a id="reserve-lr-054"></a>

### LR-054 — Identité des futurs et phases des candidats

Future inclut nœud de pile, offset, lookahead/mode, virtuel normalisé, indentation, shifts, phase, fresh et done. Le hash exclut l’identité des niveaux pour respecter l’égalité d’indentation par valeur. freeInserts est traité séparément par repair_meet. Les configurations encore pin sont exclues des sorties admises, ce qui justifie l’absence de pinTok. Réserve : begin_node et le compteur de tokens du climb sont omis, alors que les abandons futurs calculent lost depuis run.tokens()/begin_of. Il reste à prouver que deux configurations fusionnables ne peuvent pas différer sur ces coûts futurs ; l’égalité de leur seule chaîne d’états n’exprime pas cet invariant.

**Élément à établir.** Aucun couple de dérivations grammaticales atteignable montrant une fusion incorrecte n’a été établi. Réserve de preuve de dominance, pas bug démontré.

**Sources.**

- [src/engines/cursiv/lr/runtime/LrSearch.hpp:65](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/LrSearch.hpp#L65) — Clé et contrat de dominance.
- [src/engines/cursiv/lr/runtime/LrSearch.hpp:89](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/LrSearch.hpp#L89) — Hash et égalité par valeur.
- [src/engines/cursiv/lr/runtime/lr_search.hpp:80](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/lr_search.hpp#L80) — Allowance séparée.
- [src/engines/cursiv/lr/runtime/lr_search.hpp:204](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/lr_search.hpp#L204) — Admission après pin.
- [src/engines/cursiv/lr/runtime/lr_search.hpp:707](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/lr_search.hpp#L707) — Begins et tokens utilisés pour lost.


## Comparaisons Langium autonomes

Les96 questions comparatives sont incluses ici pour que les choix de correction ne dépendent pas du rapport exhaustif. Une différence documentée ou un comportement partagé ne doit pas être interprété comme une supériorité automatique de Langium.

<a id="comparison-api-001"></a>

### API-001 — Sérialisation réversible d’un modèle AST

**Question.** Comparer le contrat serialize/deserialize et ses options ; identifier les codecs supplémentaires nécessaires pour les valeurs C++ sans assimiler JsonValue à un serializer AST.

**Cursiv.** Recherche des services de sérialisation et lecture des accès de champs : la création dynamique d’un nœud est disponible, mais elle ne suffit pas à remplir ses champs et rétablir les liens. Les noms de types sont consultables ; l’identité interne reste un tag C++ dont l’adresse ne peut servir de format persistant. Les string_view imposent aussi un stockage restauré stable. La représentation JSON actuelle ne définit aucune politique pour ces cas.

**Preuves Cursiv.**

- [src/core/cursiv/syntax-tree/AstReflection.hpp:29](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/syntax-tree/AstReflection.hpp#L29) — Métadonnées publiques de propriété limitées.
- [src/core/cursiv/syntax-tree/AstMemberAccess.hpp:42](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/syntax-tree/AstMemberAccess.hpp#L42) — Les valeurs et références sont exclues du visiteur de contenance.
- [src/core/cursiv/services/JsonValue.hpp:1](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/services/JsonValue.hpp#L1) — Conteneur JSON sans codec AST.

**Comparaison.** Langium expose serialize/deserialize avec options refText, sourceText, textRegions, comments, replacer et conversion d’URI. Le codec ignore container/document/CST puis restaure la structure et des références par chemin ; il ne restitue pas un CST complet. Hydrator est une autre brique : transfert de ParseResult vers des workers, avec précondition que hydrate reçoit un résultat produit par dehydrate. Ce n’est pas un format de persistance universel et les valeurs spéciales nécessitent aussi une politique JSON.

**Preuves de comparaison.**

- [langium/packages/langium/src/serializer/json-serializer.ts:18](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/serializer/json-serializer.ts#L18) — Options du codec.
- [langium/packages/langium/src/serializer/json-serializer.ts:114](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/serializer/json-serializer.ts#L114) — Propriétés exclues et serialize/deserialize.
- [langium/packages/langium/src/serializer/json-serializer.ts:152](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/serializer/json-serializer.ts#L152) — Références URI et chemin.
- [langium/packages/langium/src/serializer/hydrator.ts:24](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/serializer/hydrator.ts#L24) — Contrat de transfert et précondition de réhydratation.

**Limites.** L’absence concerne le contrat fourni par ce snapshot ; un codec applicatif reste réalisable avec les types concrets et ses propres règles d’ownership.

<a id="comparison-api-002"></a>

### API-002 — Intégration d’un AST synthétique dans un document

**Question.** Comparer fromModel et ses préconditions, puis qualifier ce qui exige une extension de service ou une adaptation du modèle Cursiv.

**Cursiv.** La séparation factory/store permet d’ajouter un document spécialisé via Documents::addDocument. Ce point d’extension rend le cas réalisable, mais ne constitue pas un contrat d’intégration de modèle : le résultat normal est produit par AstReducer et son arena. Les services qui résolvent un nœud via nodes() ou une position via CST attendent davantage qu’un root non nul. Le modèle synthétique nécessite donc une convention et des services adaptés.

**Preuves Cursiv.**

- [src/core/cursiv/workspace/DocumentFactory.hpp:27](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DocumentFactory.hpp#L27) — Entrées textuelles et extension createDocument en ligne71.
- [src/core/cursiv/syntax-tree/ParseResult.hpp:31](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/syntax-tree/ParseResult.hpp#L31) — Arena et registre privés ; friend de test explicite.
- [src/core/cursiv/syntax-tree/AstReflection.hpp:54](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/syntax-tree/AstReflection.hpp#L54) — Factory de nœud isolé.

**Comparaison.** LangiumDocumentFactory.fromModel construit directement un ParseResult sans erreurs et un document à l’état Parsed, puis attache $document à la racine. Cette méthode n’invente pas de CST, de positions ou de texte, et n’appelle pas dans ce chemin le relink profond des $container effectué par parse ; le client reste responsable d’un modèle cohérent. L’écart Cursiv est l’absence de cette porte et de son contrat d’ownership, pas une garantie automatique de tous les services de navigation côté Langium.

**Preuves de comparaison.**

- [langium/packages/langium/src/workspace/documents.ts:213](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/workspace/documents.ts#L213) — Entrée fromModel.
- [langium/packages/langium/src/workspace/documents.ts:222](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/workspace/documents.ts#L222) — ParseResult synthétique.
- [langium/packages/langium/src/workspace/documents.ts:253](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/workspace/documents.ts#L253) — État Parsed et rattachement racine en275.

**Limites.** Aucune impossibilité de construire un AST C++ n’est affirmée. Les garanties d’une extension utilisateur dépendent de son ownership et des services qu’elle remplace.

<a id="comparison-api-003"></a>

### API-003 — Traçabilité du texte généré vers le modèle source

**Question.** Comparer les régions de trace et leur composition via traceToNode et toStringAndTrace ; distinguer cette fonction des traductions d’entrée du parseur.

**Cursiv.** La frontière a été suivie depuis le nœud Entity jusqu’au texte rendu : les noms et types deviennent des fragments sans marqueur source. size()/cut_from() pourraient aider un générateur spécifique à construire des régions, mais ni les URI, ni les propriétés, ni les régions imbriquées ne sont conservées par le framework. La normalisation des lignes rend une carte calculée avant émission insuffisante.

**Preuves Cursiv.**

- [src/core/cursiv/text/TextBuilder.hpp:62](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/text/TextBuilder.hpp#L62) — Composition immédiate sans origine.
- [src/core/cursiv/text/TextBuilder.hpp:98](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/text/TextBuilder.hpp#L98) — Extraction limitée au texte et sa taille.
- [src/core/cursiv/syntax-tree/SourceTranslation.hpp:15](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/syntax-tree/SourceTranslation.hpp#L15) — Autre direction de projection.
- [examples/domainmodel/src/cli/domainmodel/Generator.cpp:95](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/domainmodel/src/cli/domainmodel/Generator.cpp#L95) — Génération retournant une string.

**Comparaison.** Langium offre traceToNode pour nœud/propriété/index ou régions arbitraires et toStringAndTrace retourne texte et arbre TraceRegion avec sourceRegion, targetRegion et enfants. L’origine peut inclure un fileURI. Il faut produire et stocker ces traces pour chaque sortie : l’API ne déploie pas d’elle-même un débogueur ni un protocole de navigation multifichier.

**Preuves de comparaison.**

- [langium/packages/langium/src/generate/generator-node.ts:66](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/generate/generator-node.ts#L66) — Texte et arbre de traces.
- [langium/packages/langium/src/generate/generator-node.ts:886](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/generate/generator-node.ts#L886) — Trace attachée à une propriété AST.
- [langium/packages/langium/src/generate/generator-tracing.ts:15](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/generate/generator-tracing.ts#L15) — Spécifications source et régions imbriquées.

**Limites.** Ce manque n’empêche pas la génération textuelle ; il concerne la navigation et les diagnostics ramenés depuis les fichiers produits.

<a id="comparison-api-004"></a>

### API-004 — Production de règles de coloration depuis la grammaire

**Question.** Comparer les formats et les limites des générateurs TextMate, Monarch et Prism sans présumer qu’une coloration équivaut au parsing.

**Cursiv.** Les artefacts TextMate sont cohérents avec un usage de squelette, mais ne portent pas de liaison générée aux terminaux C++. Recherche des formats dans src/cmake/tools sans autre générateur dédié. Les semantic tokens sont un service différent : ils requièrent le serveur et ne suppriment pas cette description lexicale distribuée.

**Preuves Cursiv.**

- [templates/project/vscode/syntaxes/@CURSIV_NEW_ID@.tmLanguage.json:32](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/templates/project/vscode/syntaxes/@CURSIV_NEW_ID@.tmLanguage.json#L32) — Mots-clés en dur.
- [examples/domainmodel/vscode/syntaxes/domain-model.tmLanguage.json:14](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/examples/domainmodel/vscode/syntaxes/domain-model.tmLanguage.json#L14) — Liste de coloration indépendante.
- [src/core/cursiv/grammar/GrammarDump.hpp:1](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/grammar/GrammarDump.hpp#L1) — Description de grammaire, sans contrat de générateur de coloration.
- [docs/learn/editor-integration.md:50](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/docs/learn/editor-integration.md#L50) — Maintenance TextMate explicitement à la charge de l’auteur.

**Comparaison.** Langium CLI fournit trois générateurs : TextMate dérive mots-clés, chaînes et commentaires ; Monarch extrait catégories/règles ; Prism conserve une sélection des terminaux et indique explicitement un résultat best-effort. Ils réduisent les listes manuelles mais ne promettent pas l’équivalence avec le parseur, notamment pour contextualité et constructions syntaxiques complexes.

**Preuves de comparaison.**

- [langium/packages/langium-cli/src/generator/highlighting/textmate-generator.ts:50](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium-cli/src/generator/highlighting/textmate-generator.ts#L50) — Extraction depuis Grammar.
- [langium/packages/langium-cli/src/generator/highlighting/monarch-generator.ts:110](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium-cli/src/generator/highlighting/monarch-generator.ts#L110) — Génération Monarch.
- [langium/packages/langium-cli/src/generator/highlighting/prism-generator.ts:19](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium-cli/src/generator/highlighting/prism-generator.ts#L19) — Extraction et avertissement best-effort en55.

**Limites.** Pas de jugement d’exactitude pour toutes les regex éditeur ni de comparaison visuelle exécutée.

<a id="comparison-api-005"></a>

### API-005 — Cycle de vie d’un notebook et de ses cellules

**Question.** Comparer NormalizedNotebookDocuments et les branchements nécessaires ; séparer la brique de synchronisation de la sémantique propre au notebook.

**Cursiv.** Recherche de notebook dans src et lecture de la surface LSP/documentaire : les identités URI virtuelles permettent de représenter le texte isolé, sans notion de cellule ordonnée ou de notebook parent. Le point manquant est le cycle structurel et son raccord au store ; la sémantique des dépendances entre cellules relève ensuite du langage.

**Preuves Cursiv.**

- [src/lsp/cursiv/services/SharedLspServices.hpp:28](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/services/SharedLspServices.hpp#L28) — Surface des services partagés.
- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:275](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L275) — Annonce de synchronisation texte.
- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:545](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L545) — Ouverture de document individuel.
- [src/core/cursiv/workspace/Documents.hpp:32](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/Documents.hpp#L32) — Store indexé par URI/document.

**Comparaison.** Langium installe NormalizedNotebookDocuments dans le module LSP partagé. Cette classe offre maps notebook/cellules et listen(connection) pour traduire ouvertures, changements structurels/textuels et fermetures vers le gestionnaire texte. Sa présence dans le conteneur ne suffit pas : le serveur consommateur doit brancher listen et annoncer les capacités de notebook adaptées ; startLanguageServer ne le fait pas automatiquement dans le fichier inspecté. Elle ne définit pas la visibilité entre cellules.

**Preuves de comparaison.**

- [langium/packages/langium/src/lsp/default-lsp-module.ts:100](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/default-lsp-module.ts#L100) — Service NotebookDocuments installé.
- [langium/packages/langium/src/lsp/normalized-text-documents.ts:300](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/normalized-text-documents.ts#L300) — Gestionnaire et maps.
- [langium/packages/langium/src/lsp/normalized-text-documents.ts:370](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/normalized-text-documents.ts#L370) — Branchement explicite des handlers.
- [langium/packages/langium/src/lsp/normalized-text-documents.ts:407](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/normalized-text-documents.ts#L407) — Mise à jour de la structure de cellules.

**Limites.** Aucune incompatibilité intrinsèque avec une extension de serveur notebook ; c’est une brique à réaliser. Pas d’essai client.

<a id="comparison-api-006"></a>

### API-006 — Choix d’une règle d’entrée pour un fragment

**Question.** Comparer ParserOptions et le choix de règle à l’appel ; préciser les contraintes de types et de CST des deux API.

**Cursiv.** L’API effacée stabilise le résultat entre moteurs mais ne porte pas d’identifiant de règle à invoquer. La sélection d’un root se fait au niveau de la déclaration/génération du langage. Reconfigurer le type de résultat C++ seul n’en fait pas une autre règle d’entrée ; les tables et leurs estampilles doivent correspondre.

**Preuves Cursiv.**

- [src/core/cursiv/parser/Parser.hpp:23](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/parser/Parser.hpp#L23) — Commentaire getEntryRule et signature parse.
- [src/core/cursiv/grammar/Grammar.hpp:17](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/grammar/Grammar.hpp#L17) — Surface effectivement exposée.
- [src/engines/cursiv/ll/runtime/LlLanguageParser.hpp:52](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/ll/runtime/LlLanguageParser.hpp#L52) — Drive fixé à la construction.
- [src/engines/cursiv/lr/runtime/LrLanguageParser.hpp:39](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/LrLanguageParser.hpp#L39) — Drive fixé à la construction.

**Comparaison.** ParserOptions.rule est une chaîne. LangiumParser.parse choisit allRules.get(options.rule), lève si la règle est absente, puis construit le CST et rattache les containers. Le paramètre générique T décrit le type attendu par l’appelant, il ne vérifie pas à lui seul que le nom choisi produit ce type. La différence est donc un point d’entrée public, avec responsabilités de typage et de choix du fragment.

**Preuves de comparaison.**

- [langium/packages/langium/src/parser/langium-parser.ts:204](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/parser/langium-parser.ts#L204) — Option rule.
- [langium/packages/langium/src/parser/langium-parser.ts:276](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/parser/langium-parser.ts#L276) — Sélection, erreur explicite et construction.

**Limites.** Le coût de génération de parseurs dédiés n’est pas mesuré. Aucune impossibilité algorithmique de multipoints d’entrée n’est affirmée.

<a id="comparison-api-007"></a>

### API-007 — Contrôle public de la récupération d’erreurs

**Question.** Comparer configuration publique du parseur et passage à Chevrotain, sans assimiler les mécanismes ni les garanties de récupération des moteurs.

**Cursiv.** Les contrôles existants ont été suivis jusqu’aux deux constructeurs moteurs pour éviter une fausse absence : Cursiv possède un budget de travail saturant partagé et des indicateurs de résultat. La limite est sa présentation aux consommateurs de services et l’absence de contrat strict commun. La justesse de chaque décompte/coupure est couverte séparément par COM, LL et LR. DiagnosticKind::bounded identifie déjà les spans arrêtés par la borne de réparation ; l’audit ne conclut pas à une coupure entièrement silencieuse.

**Preuves Cursiv.**

- [src/core/cursiv/parser/Parser.hpp:31](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/parser/Parser.hpp#L31) — Signature commune sans politique.
- [src/engines/cursiv/shared/runtime/RepairBudget.hpp:74](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/shared/runtime/RepairBudget.hpp#L74) — Bornes et structure de configuration.
- [src/engines/cursiv/ll/runtime/LlLanguageParser.hpp:52](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/ll/runtime/LlLanguageParser.hpp#L52) — Budget configurable côté LL.
- [src/engines/cursiv/lr/runtime/LrLanguageParser.hpp:39](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/LrLanguageParser.hpp#L39) — Budget configurable côté LR.
- [src/core/cursiv/syntax-tree/ParseResult.hpp:78](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/syntax-tree/ParseResult.hpp#L78) — Diagnostic/erreur/accepted observables.
- [src/core/cursiv/syntax-tree/ParseDiagnostic.hpp:29](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/syntax-tree/ParseDiagnostic.hpp#L29) — Le diagnostic bounded existe déjà.
- [src/engines/cursiv/shared/gen/CommonArtifacts.cpp:88](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/shared/gen/CommonArtifacts.cpp#L88) — Factory générée sans option.
- [src/engines/cursiv/shared/gen/CommonArtifacts.cpp:120](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/shared/gen/CommonArtifacts.cpp#L120) — Construction avec budget moteur par défaut.

**Comparaison.** Langium réexporte IParserConfig de Chevrotain et l’expose comme service parser.ParserConfig. Le wrapper fixe recoveryEnabled=true par défaut puis étale ...config après les valeurs par défaut : la récupération peut donc être désactivée à la configuration du langage. ParserOptions à l’appel ne contient que rule. Ce contrat n’offre pas pour autant les mêmes quotas de recherche que RepairBudget ni une meilleure borne de latence universelle.

**Preuves de comparaison.**

- [langium/packages/langium/src/parser/parser-config.ts:7](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/parser/parser-config.ts#L7) — Type de configuration public.
- [langium/packages/langium/src/services.ts:50](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/services.ts#L50) — Slot ParserConfig.
- [langium/packages/langium/src/parser/langium-parser.ts:812](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/parser/langium-parser.ts#L812) — Valeurs par défaut.
- [langium/packages/langium/src/parser/langium-parser.ts:828](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/parser/langium-parser.ts#L828) — Surcharge finale par config.

**Limites.** Ni le budget Cursiv ni la configuration Chevrotain ne constituent une garantie temporelle murale. Aucune exécution de parsing invalide.

<a id="comparison-ast-004"></a>

### AST-004 — Accès réflexif aux membres

**Question.** Comparer accès générique, présence des propriétés et cardinalités pour les outils indépendants d’un langage.

**Cursiv.** Les traits séparent explicitement stockage direct/optional/vector de la nature value/node/reference/variant. string reste une valeur et vector<AstNode par valeur> est refusé statiquement. Le visiteur de contenance ignore les scalaires et références ; il énumère seulement les pointeurs de nœud non nuls, avec l’index originel de chaque liste et kNoIndex pour un membre unique. Pour un sous-type absent des tables, visitUnknown vérifie le propriétaire de chaque champ par son tag avant de lire les membres hérités. La validité de ce dispatch reste dépendante de l’identité de type examinée dans AST-003.

**Preuves Cursiv.**

- [src/core/cursiv/syntax-tree/AstField.hpp:71](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/syntax-tree/AstField.hpp#L71) — Classification du stockage et refus des nœuds copiés en liste.
- [src/core/cursiv/syntax-tree/AstMemberAccess.hpp:16](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/syntax-tree/AstMemberAccess.hpp#L16) — Visite des variants et autres contenances.
- [src/core/cursiv/syntax-tree/AstMemberAccess.hpp:87](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/syntax-tree/AstMemberAccess.hpp#L87) — Visiteur de sous-type inconnu.
- [src/core/cursiv/syntax-tree/MemberTraits.hpp:26](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/syntax-tree/MemberTraits.hpp#L26) — Le nom décoratif ne sert pas de clé.

**Comparaison.** Langium décrit les propriétés et leur caractère optional dans sa réflexion runtime ; isComplete vérifie les propriétés requises d’un objet AST incomplet. Cursiv connaît cardinalité et nature au niveau des traits C++ mais le visiteur public examine les enfants seulement et PropertyMetaData n’expose pas cette cardinalité. Les algorithmes génériques qui inspectent valeurs et complétude doivent donc obtenir des métadonnées supplémentaires ; le besoin de codec est traité en API-001.

**Preuves de comparaison.**

- [langium/packages/langium/src/syntax-tree.ts:167](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/syntax-tree.ts#L167) — Surface runtime de réflexion.
- [langium/packages/langium/src/syntax-tree.ts:261](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/syntax-tree.ts#L261) — Contrôle des propriétés requises.

**Limites.** Les listes sont indexées en uint32 et dépendent des limites des arbres construits. Cette API est une visite de contenance, pas une sérialisation générique de toutes les valeurs.

<a id="comparison-ast-005"></a>

### AST-005 — Réflexion et filtrage de types

**Question.** Comparer sous-typage et types admissibles pour une référence ou une propriété.

**Cursiv.** La table de tags est construite une fois, avec une capacité puissance de deux au moins double du nombre de types ; un slot vide termine les probes. L’égalité finale compare les adresses exactes. Les accès aux indices externes renvoient vide/null hors du domaine et le filtrage de sous-type repose sur la fermeture des bases du tag. Les lignes de propriétés et de références incluent les champs hérités précompilés ; referenceInfo ne fabrique un accès que pour un handle dont owner et field sont admissibles. Le supertype null signifie explicitement tout accepter.

**Preuves Cursiv.**

- [src/core/cursiv/syntax-tree/DefaultAstReflection.cpp:15](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/syntax-tree/DefaultAstReflection.cpp#L15) — Capacité et insertion de la table de tags.
- [src/core/cursiv/syntax-tree/DefaultAstReflection.hpp:25](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/syntax-tree/DefaultAstReflection.hpp#L25) — Sous-typage et factory.
- [src/core/cursiv/syntax-tree/DefaultAstReflection.hpp:72](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/syntax-tree/DefaultAstReflection.hpp#L72) — Probe exact et sortie sur slot vide.
- [src/core/cursiv/syntax-tree/DefaultAstReflection.cpp:119](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/syntax-tree/DefaultAstReflection.cpp#L119) — AcceptSet et ReferenceInfo.
- [src/core/cursiv/syntax-tree/AstMetaData.hpp:93](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/syntax-tree/AstMetaData.hpp#L93) — Décodage de types et familles.

**Comparaison.** Langium identifie les types par chaînes et mémorise les réponses de sous-typage calculées récursivement. Cursiv utilise les tags et une fermeture précompilée, puis des AcceptSet de références. Les interfaces ont des issues différentes pour une propriété de référence inconnue : Langium getReferenceType lève, Cursiv fournit une sentinelle ou un ensemble vide selon le contrat de la méthode. Les types multi-cibles et le narrowing Cursiv ne se ramènent pas mécaniquement au nom unique retourné par getReferenceType.

**Preuves de comparaison.**

- [langium/packages/langium/src/syntax-tree.ts:192](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/syntax-tree.ts#L192) — Référence inconnue refusée explicitement.
- [langium/packages/langium/src/syntax-tree.ts:220](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/syntax-tree.ts#L220) — Cache du sous-typage et fermeture calculée.

**Limites.** Des métadonnées construites à la main incohérentes ne sont pas validées en profondeur. Les tags incorrectement copiés relèvent du défaut AST-003 ; aucun défaut supplémentaire de recherche n’est établi.

<a id="comparison-ast-007"></a>

### AST-007 — Propriété d'un ParseResult et vues empruntées

**Question.** Comparer la durée de vie du résultat et de son texte ; expliciter les contraintes d’emprunt imposées aux clients C++.

**Cursiv.** ParseResult garde le Cst dans un unique_ptr : déplacer le résultat ne déplace ni le CST ni les octets du pool. L’assignation détruit d’abord les anciens nœuds et le vecteur PMR, transfère le pool, puis reconstruit le vecteur avec son allocateur d’origine. Le self-move est neutralisé et value de la source est remis à null. Document détruit les nœuds avant que le buffer texte ne disparaisse. Les string_view du résultat restent empruntées au texte fourni ; un résultat déplacé ne prolonge pas ce texte.

**Preuves Cursiv.**

- [src/core/cursiv/syntax-tree/ParseResult.cpp:46](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/syntax-tree/ParseResult.cpp#L46) — Ordre de déplacement et reconstruction du vecteur PMR.
- [src/core/cursiv/syntax-tree/ParseResult.hpp:26](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/syntax-tree/ParseResult.hpp#L26) — Adresse stable et ordre des membres.
- [src/core/cursiv/workspace/Document.cpp:32](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/Document.cpp#L32) — Destruction avant le buffer texte.
- [src/core/cursiv/parser/Parser.hpp:28](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/parser/Parser.hpp#L28) — Contrat explicite du texte emprunté.

**Comparaison.** Le CST Langium conserve fullText à sa racine dans un graphe JavaScript géré par le ramasse-miettes ; un ParseResult et ses nœuds restent des objets référencés. Cursiv sépare le résultat déplaçable du texte emprunté et exige que le client garde ce texte vivant, ou utilise Document qui possède les deux. La durée de vie implicite du graphe Langium ne se transpose donc pas à un string_view C++ extrait du résultat.

**Preuves de comparaison.**

- [langium/packages/langium/src/parser/langium-parser.ts:276](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/parser/langium-parser.ts#L276) — Construction de la racine avec le texte source.
- [langium/packages/langium/src/workspace/documents.ts:280](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/workspace/documents.ts#L280) — Texte conservé sur la racine CST.

**Limites.** Les vues sur le résultat remplacé sont invalidées. Le résultat moved-from ne fournit plus de CST à déréférencer ; la publication concurrente du document relève du workspace.

<a id="comparison-ast-010"></a>

### AST-010 — Conversions scalaires et erreurs de conversion

**Question.** Comparer les conversions personnalisables et la remontée des échecs à partir d’un token.

**Cursiv.** La conversion numérique exige la consommation complète, traite le plus initial une seule fois, refuse les doubles signes et conserve les erreurs de plage. Le fast path double limite significande et exposant et retombe sur from_chars si l’arrondi hôte ne convient pas ; float évite le double arrondi. Les booléens acceptent la casse ASCII et les enum utilisent les noms sondés. ConversionResult produit un diagnostic au record et laisse le membre précédent si la conversion échoue. Une string possédée convertie en string_view est copiée dans le pool du CST. La propagation des exceptions de converters est distincte du canal ConversionResult. Les grandes marches textuelles n’observent pas le token interne.

**Preuves Cursiv.**

- [src/core/cursiv/grammar/Conversion.hpp:109](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/grammar/Conversion.hpp#L109) — Fast path décimal borné.
- [src/core/cursiv/grammar/Conversion.hpp:189](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/grammar/Conversion.hpp#L189) — Conversions, consommation complète et erreurs.
- [src/engines/cursiv/shared/runtime/AstConvThunks.hpp:352](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/shared/runtime/AstConvThunks.hpp#L352) — Canal de diagnostic.
- [src/engines/cursiv/shared/runtime/AstConvThunks.hpp:434](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/shared/runtime/AstConvThunks.hpp#L434) — Conservation des bytes pour string_view.
- [src/engines/cursiv/shared/runtime/AstConvThunks.hpp:119](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/shared/runtime/AstConvThunks.hpp#L119) — Collecte des parties sans poll.

**Comparaison.** Langium choisit le converter d’abord par nom INT/STRING/ID, puis par type primitif. INT utilise parseInt, number utilise Number et boolean teste toLowerCase()==true. Cursiv convertit vers les types C++ des champs et exige la consommation numérique complète, avec erreurs de plage et ConversionResult. Les terminaux habituels restreignent déjà les entrées ; les différences deviennent visibles avec des terminaux personnalisés plus permissifs. Langium propose aussi bigint/Date ; leur équivalent C++ exige une conversion propre au langage.

**Preuves de comparaison.**

- [langium/packages/langium/src/parser/value-converter.ts:42](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/parser/value-converter.ts#L42) — Ordre de dispatch.
- [langium/packages/langium/src/parser/value-converter.ts:95](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/parser/value-converter.ts#L95) — Conversions numériques, bigint, date et booléen.

**Limites.** Aucune exactitude exhaustive de toutes les combinaisons de décimaux/plateformes n’est prouvée. Le périmètre ne valide pas les callbacks métier arbitraires.

<a id="comparison-cac-011"></a>

### CAC-011 — Factory de mémoïsation partagée

**Question.** Comparer le contrat de calcul de valeur et d’erreur ; les garanties de concurrence C++ demandent une analyse propre.

**Cursiv.** get fait la recherche puis capture la génération avant d’invoquer le provider hors de tout guard/table lock. Plusieurs misses peuvent donc calculer concurremment, mais insert retourne la valeur du premier gagnant ; le provider est explicitement tenu au déterminisme par clé. Un clear intervenant après la capture empêche la mémorisation du calcul ancien tout en le retournant au caller. Une exception du provider ne revendique aucun slot. Cette API diffère volontairement de l’ensemble has/set/delete de Langium.

**Preuves Cursiv.**

- [src/core/cursiv/utils/SimpleCache.hpp:26](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/utils/SimpleCache.hpp#L26) — Ordre hit/génération/provider/insert.
- [src/core/cursiv/utils/SimpleCache.hpp:29](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/utils/SimpleCache.hpp#L29) — Concurrence et déterminisme documentés.

**Comparaison.** Langium utilise Map, propose has/set/delete et garde directement le résultat du provider synchrone. Cursiv expose une mémoïsation concurrente get/clear avec copie de V et filtre de génération ; un port doit traiter déterminisme, ownership et absence de set/erase au lieu de traduire mécaniquement l’API.

**Preuves de comparaison.**

- [langium/packages/langium/src/utils/caching.ts:37](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/utils/caching.ts#L37) — SimpleCache Map et opérations publiques.
- [langium/packages/langium/src/utils/caching.ts:52](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/utils/caching.ts#L52) — Calcul et insertion synchrone.

**Limites.** Le cache n’assure pas à lui seul la fraîcheur du document lu par le provider ; les ressources empruntées par V doivent rester vivantes. Contraintes Hash : CAC-001.

<a id="comparison-cac-013"></a>

### CAC-013 — Invalidation et abonnement du cache documentaire

**Question.** Comparer les événements d’invalidation et le désabonnement, sans présumer identiques les modèles d’exécution.

**Cursiv.** Le constructeur rejette les états autres que Linked/IndexedReferences, capture le builder utilisé puis installe les listeners avec rollback du premier si le second échoue. Le destructeur se désabonne de ce builder capturé. Sans phase, changed et deleted sont traduits en DocumentId ; avec phase, les changements attendent la phase et les suppressions sont toujours vidées. Cette politique convient aux dépendances du document et à ses cibles directement relinkées, pas à une dépendance transitive arbitraire ; le header le précise.

**Preuves Cursiv.**

- [src/core/cursiv/utils/DocumentCache.hpp:162](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/utils/DocumentCache.hpp#L162) — Validation de phase et abonnement transactionnel.
- [src/core/cursiv/utils/DocumentCache.hpp:182](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/utils/DocumentCache.hpp#L182) — Désabonnement de _builder capturé.
- [src/core/cursiv/utils/DocumentCache.hpp:191](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/utils/DocumentCache.hpp#L191) — Invalidation changement/suppression.
- [src/core/cursiv/utils/DocumentCache.hpp:36](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/utils/DocumentCache.hpp#L36) — Portée autorisée des dépendances.

**Comparaison.** Les deux caches se raccordent aux événements de document et toujours aux suppressions. Langium partitionne par URI et efface la Map de contexte ; Cursiv partitionne par DocumentId stable, conserve sa table et restreint explicitement les phases d’invalidation. Cette différence impose un choix de phase et de dépendances lors d’un port.

**Preuves de comparaison.**

- [langium/packages/langium/src/utils/caching.ts:158](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/utils/caching.ts#L158) — Abonnements onDocumentPhase/onUpdate.
- [langium/packages/langium/src/utils/caching.ts:119](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/utils/caching.ts#L119) — clear du contexte retire sa Map.

**Limites.** La validité dépend de la durée de vie du builder et de la livraison des événements (WSP-012) ; remplacer/détruire le builder avant le cache reste hors du contrat d’ownership.

<a id="comparison-cac-014"></a>

### CAC-014 — Invalidation du cache workspace

**Question.** Comparer les changements qui rendent une valeur globale obsolète.

**Cursiv.** WorkspaceCache rejette Changed parce que cette phase n’est pas publiée. Il installe le listener de phase puis celui d’update avec rollback, et les retire au destructeur. Sans phase chaque update vide le cache ; avec phase, seule la phase demandée ou une suppression l’invalide. Un appel pendant un workspace partiellement construit peut donc mémoriser un état transitoire si aucune phase appropriée n’a été choisie ; cette responsabilité est documentée et le cache ne remplace pas un prérequis de lecture.

**Preuves Cursiv.**

- [src/core/cursiv/utils/WorkspaceCache.hpp:19](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/utils/WorkspaceCache.hpp#L19) — Contrat de phase et lecture partielle.
- [src/core/cursiv/utils/WorkspaceCache.hpp:42](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/utils/WorkspaceCache.hpp#L42) — Deux chemins d’invalidation.
- [src/core/cursiv/utils/WorkspaceCache.hpp:59](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/utils/WorkspaceCache.hpp#L59) — Changed rejeté, installation et rollback.
- [src/core/cursiv/utils/WorkspaceCache.hpp:77](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/utils/WorkspaceCache.hpp#L77) — Désabonnement.

**Comparaison.** Même structure événementielle que Langium : onBuildPhase quand un état est choisi, sinon onUpdate, avec invalidation immédiate des suppressions. Cursiv ajoute le contrôle explicite de Changed et la concurrence de SimpleCache ; il faut conserver le contexte de lecture requis par la factory.

**Preuves de comparaison.**

- [langium/packages/langium/src/utils/caching.ts:194](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/utils/caching.ts#L194) — Abonnements de WorkspaceCache.

**Limites.** Pas de constat autonome ; dépend de l’ordre des hooks du builder et des dépendances du client. Le validateur requirements utilisant ce mécanisme relève du lot des exemples.

<a id="comparison-cst-005"></a>

### CST-005 — Navigation parents, enfants et voisinage

**Question.** Comparer les opérations de navigation requises par les providers et leur comportement aux frontières des tokens.

**Cursiv.** Les recherches par feature et par mot-clé appliquent volontairement un ordre différent entre reconnaissance du record et frontière de propriétaire AST : une écriture de membre peut être le record qui ouvre le nœud enfant, tandis qu’un mot-clé du nœud enfant ne doit pas être volé par son parent. Le parcours recule par sous-arbre sans récursion puis inverse les résultats pour l’ordre source. find_leaf_before_offset ignore trivia, réparations et étendues vides ; word_at traite séparément la fin d’un mot et son début. L’index de feature hors limites est explicitement rabattu sur le dernier résultat. Le raccord formatting révèle cependant que spells_keyword compare le texte source au littéral avec égalité sensible à la casse. Une variante de ikw est reconnue par le parseur mais n’est pas retrouvée par keyword("littéral"); le constat partagé est porté par LSP-035.

**Preuves Cursiv.**

- [src/core/cursiv/syntax-tree/CstUtils.cpp:45](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/syntax-tree/CstUtils.cpp#L45) — Marche selon les frontières de modèle.
- [src/core/cursiv/syntax-tree/CstUtils.cpp:103](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/syntax-tree/CstUtils.cpp#L103) — Clamping de l’index.
- [src/core/cursiv/syntax-tree/CstUtils.cpp:181](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/syntax-tree/CstUtils.cpp#L181) — Voisin précédent et filtres.
- [src/core/cursiv/syntax-tree/CstUtils.cpp:210](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/syntax-tree/CstUtils.cpp#L210) — Mot sous le caret.
- [src/core/cursiv/syntax-tree/CstNodeView.hpp:20](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/syntax-tree/CstNodeView.hpp#L20) — Validité bornée de la vue.
- [src/core/cursiv/syntax-tree/CstUtils.cpp:111](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/syntax-tree/CstUtils.cpp#L111) — Égalité exacte de la graphie source du mot-clé.

**Comparaison.** Langium distingue findLeafNodeAtOffset et BeforeOffset par recherche dans les enfants, ajuste la fin des noms dans findDeclarationNodeAtOffset, et laisse les helpers de voisinage inclure les nœuds hidden par défaut. Cursiv filtre explicitement trivia, réparations et spans vides dans sa recherche de feuille précédente. Les noms proches des API ne garantissent donc pas les mêmes résultats à une frontière de commentaire, de réparation ou en fin de nom ; le portage doit choisir la convention voulue. Pour les sélecteurs de mots-clés, Langium compare la valeur du Keyword de grammaire et peut retrouver les variantes de casse ; cette divergence est détaillée dans LSP-035.

**Preuves de comparaison.**

- [langium/packages/langium/src/utils/cst-utils.ts:167](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/utils/cst-utils.ts#L167) — Ajustement de frontière de nom.
- [langium/packages/langium/src/utils/cst-utils.ts:215](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/utils/cst-utils.ts#L215) — AtOffset et BeforeOffset en237.
- [langium/packages/langium/src/utils/cst-utils.ts:276](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/utils/cst-utils.ts#L276) — Voisin précédent avec hidden=true par défaut.

**Limites.** Ces recherches supposent les subtreeSize et fins monotones du builder. La comparaison établit les conventions inspectées sans couvrir toutes les combinaisons de CST réparés.

<a id="comparison-gra-001"></a>

### GRA-001 — Normalisation des expressions du DSL

**Question.** Comparer le sens des opérateurs, options et cardinalités ; distinguer syntaxe C++ et expressivité du langage décrit.

**Cursiv.** FlatCat aplatit séquences et choix sans permuter les branches. Le fold des créations conserve un préfixe nu pour l’adoption, l’absorbe dans la première création et ferme la région au nest suivant ; deux créations ou une création après nest sont refusées. Les répétitions conservent Min/Max et ne se simplifient pas abusivement en une cardinalité différente. ExprShape refuse un combinator inconnu plutôt que de lui inventer zéro enfant ; ses rebind conservent tags, membres, prose et bornes.

**Preuves Cursiv.**

- [src/core/cursiv/grammar/expr.hpp:514](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/grammar/expr.hpp#L514) — Aplatissement et algèbre des créations.
- [src/core/cursiv/grammar/expr.hpp:647](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/grammar/expr.hpp#L647) — Refus de deux créations.
- [src/core/cursiv/grammar/expr.hpp:727](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/grammar/expr.hpp#L727) — Composition et choix ordonné.
- [src/core/cursiv/grammar/ExprShape.hpp:14](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/grammar/ExprShape.hpp#L14) — Vocabulaire explicite de traversée.

**Comparaison.** Langium décrit également groupes, alternatives, actions et cardinalités ?/*/+. Cursiv ajoute repeat<Min,Max>, des créations/nests typés C++ et un choix dont le premier bras tranche les égalités ; ce n’est pas une traduction textuelle directe des actions et prédicats Langium.

**Preuves de comparaison.**

- [langium/packages/langium/src/grammar/langium-grammar.langium:96](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/grammar/langium-grammar.langium#L96) — Alternatives, groupes, cardinalités et actions.

**Limites.** L’équivalence de toutes les expressions possibles n’est pas prouvée ; les restrictions de chaque moteur sont auditées dans leurs lots.

<a id="comparison-gra-002"></a>

### GRA-002 — Identité des règles et composition de grammaires

**Question.** Comparer les contrats de réutilisation et de spécialisation, y compris l’identité des règles exposées aux services.

**Cursiv.** L’identité déclarée est préservée récursivement à travers les stages ; les corps effectifs, types et converters suivent la déclaration d’override pertinente. one_per_decl déduplique les deux chemins d’un diamant qui rejoignent réellement la même déclaration. InGrammar ajoute la grammaire d’origine à l’identité, requalifie les références et garde ses frontières de mots/trivia ; l’indentation imbriquée est refusée explicitement. Le cas de deux overrides parents distincts révèle le défaut confirmé par une seconde lecture indépendante.

**Preuves Cursiv.**

- [src/core/cursiv/grammar/GrammarSpec.hpp:371](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/grammar/GrammarSpec.hpp#L371) — Identité déclarée versus effective.
- [src/core/cursiv/grammar/GrammarUtils.hpp:51](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/grammar/GrammarUtils.hpp#L51) — Diamants et sélection du parent.
- [src/core/cursiv/grammar/InGrammar.hpp:24](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/grammar/InGrammar.hpp#L24) — Résolution et isolation de la grammaire embarquée.

**Comparaison.** Langium réutilise des grammaires via imports, fragments et paramètres booléens de règles. Le mécanisme Cursiv extends/override/super et section<G> apporte une composition différente ; un portage doit reconstruire ces relations, sans supposer une identité d’API ou les mêmes règles de résolution.

**Preuves de comparaison.**

- [langium/packages/langium/src/grammar/langium-grammar.langium:65](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/grammar/langium-grammar.langium#L65) — Imports, fragments et paramètres.
- [langium/packages/langium/src/grammar/langium-grammar.langium:134](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/grammar/langium-grammar.langium#L134) — Appels paramétrés.

**Limites.** Aucun compilateur lancé ; le défaut découle de la dépendance obligatoire des arguments de template.

<a id="comparison-gra-003"></a>

### GRA-003 — Directives d'affectation et association AST

**Question.** Comparer directives et formes de modèle produites sans confondre génération de classes et association à des classes C++.

**Cursiv.** payload_of distingue nœud et texte et distribue les alternatives. La validation examine chaque bras, refuse zéro/deux nœuds quand un seul est attendu, append hors vector, ambiguïtés de variant, rétrécissement numérique et string_view provenant d’un texte propriétaire temporaire. Les conversions par défaut exigent la consommation complète, bornent les entiers via from_chars, distinguent vrai/faux et autorisent une spécialisation. Le chemin décimal rapide borne significande/exposant et retombe sur la bibliothèque quand le mode d’arrondi ne convient pas.

**Preuves Cursiv.**

- [src/core/cursiv/grammar/PayloadOf.hpp:43](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/grammar/PayloadOf.hpp#L43) — Nature des valeurs produites.
- [src/engines/cursiv/shared/gen/ast_refusals.hpp:172](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/shared/gen/ast_refusals.hpp#L172) — Compatibilité des types et ownership.
- [src/engines/cursiv/shared/gen/ast_refusals.hpp:340](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/shared/gen/ast_refusals.hpp#L340) — Cardinalité par alternative.
- [src/core/cursiv/grammar/Conversion.hpp:189](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/grammar/Conversion.hpp#L189) — Conversions et diagnostics.

**Comparaison.** Langium utilise =, += et ?=, génère ou infère les types de propriétés, et fournit un ValueConverter. Cursiv cible des membres C++ déjà déclarés et vérifie plus tôt stockage/constructibilité/ownership. Les types bigint/Date de Langium exigent une représentation et un converter personnalisés en Cursiv.

**Preuves de comparaison.**

- [langium/packages/langium/src/grammar/langium-grammar.langium:164](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/grammar/langium-grammar.langium#L164) — Affectations et références.
- [langium/packages/langium/src/grammar/langium-grammar.langium:56](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/grammar/langium-grammar.langium#L56) — Types primitifs Langium.

**Limites.** Les converters utilisateur peuvent avoir leurs propres préconditions et exceptions ; la correction arithmétique complète de la bibliothèque standard n’est pas auditée.

<a id="comparison-gra-007"></a>

### GRA-007 — Entrées et introspection de la grammaire

**Question.** Comparer les informations réellement consultables à l’exécution, en distinguant image grammaticale, métadonnées et modèle source complet.

**Cursiv.** Les identités de terminal sont interprétées via terminalName/Spellings/Doc ; les records via les méthodes spécifiques writes/fills/rule/feature, sans confondre leurs espaces d’IDs. NextFeature transporte propriété, type et narrow pour conserver le contexte d’une référence même avant la construction du nœud. Les métadonnées générées sont constantes et leur durée de vie couvre les résultats. Le commentaire Parser évoquant getEntryRule est périmé : cette méthode ne figure pas dans Grammar.

**Preuves Cursiv.**

- [src/core/cursiv/grammar/Grammar.hpp:20](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/grammar/Grammar.hpp#L20) — Espaces d’identités séparés.
- [src/core/cursiv/grammar/GrammarMetaData.hpp:25](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/grammar/GrammarMetaData.hpp#L25) — Tables runtime.
- [src/core/cursiv/grammar/NextFeature.hpp:16](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/grammar/NextFeature.hpp#L16) — Contexte des continuations.
- [src/core/cursiv/parser/Parser.hpp:23](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/parser/Parser.hpp#L23) — Référence documentaire à une méthode absente.

**Comparaison.** LangiumGeneratedCoreServices fournit directement Grammar, AST de la grammaire, que des providers peuvent parcourir. Cursiv propose une surface plus étroite orientée résultats de parsing ; cet écart est gênant pour les outils indépendants d’une langue.

**Preuves de comparaison.**

- [langium/packages/langium/src/services.ts:47](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/services.ts#L47) — Grammar comme service généré.

**Limites.** La stabilité des numéros entre deux générations n’est pas promise ; ils ne constituent pas des identifiants persistants.

<a id="comparison-gra-008"></a>

### GRA-008 — Texte source et dump de grammaire

**Question.** Comparer le texte de grammaire exposé et la possibilité de retrouver une représentation exploitable par l’outillage.

**Cursiv.** Le printer conserve les priorités par parenthèses, les bornes de répétition, les directives et les métadonnées infix ; les règles anonymes reçoivent un nom synthétique et les collisions de noms courts un nom qualifié. render ne garde pas de référence de vecteur pendant un ajout de règle. DefaultGrammarSource calcule la fermeture avec file et visited, donc les cycles terminent. Le texte dit « written » est déjà après résolution as/commit par Rule::body ; il ne reconstitue pas le fichier C++ original. dump_wrapped est une présentation humaine dont le retour à la ligne peut intervenir dans une longue chaîne contenant des espaces.

**Preuves Cursiv.**

- [src/core/cursiv/grammar/GrammarPrinter.cpp:132](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/grammar/GrammarPrinter.cpp#L132) — Noms et dépendances.
- [src/core/cursiv/grammar/GrammarPrinter.cpp:297](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/grammar/GrammarPrinter.cpp#L297) — Priorités et directives.
- [src/core/cursiv/grammar/GrammarPrinter.cpp:607](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/grammar/GrammarPrinter.cpp#L607) — Publication des offsets du dump.
- [src/core/cursiv/grammar/DefaultGrammarSource.cpp:23](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/grammar/DefaultGrammarSource.cpp#L23) — Fermeture cyclique bornée.
- [src/core/cursiv/grammar/GrammarSpec.hpp:138](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/grammar/GrammarSpec.hpp#L138) — Corps déjà réécrit.

**Comparaison.** Langium dispose d’un AST Grammar et d’un serializer de cette grammaire destiné à sa restitution runtime. Cursiv expose un dump et ses dépendances, avec informations de type C++ mais sans modèle source complet à recharger. La comparaison doit distinguer lisibilité du dump et réversibilité.

**Preuves de comparaison.**

- [langium/packages/langium/src/services.ts:47](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/services.ts#L47) — Modèle Grammar public.
- [langium/packages/langium/src/grammar/langium-grammar.langium:11](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/grammar/langium-grammar.langium#L11) — Structure formelle du modèle de grammaire.

**Limites.** Le dump ne garantit pas un round-trip de code C++, ni la restitution du texte des converters ; son format ne remplace pas l’introspection structurelle.

<a id="comparison-lex-012"></a>

### LEX-012 — Scan, plus longue lecture et élection du token

**Question.** Comparer le token choisi en cas de concurrence observable ; conserver la distinction entre élection lexicale et choix syntaxique.

**Cursiv.** LexMatch conserve la dernière acceptation admissible. Les relais se comparent par longueur, réservation et tier ; chaque refus de tête retire monotonement un terminal avant relecture. Le mode longer gagne à longueur strictement supérieure et ne remplace pas le trivia déjà élu. halt conserve la portée des frontières et relectures, distincte de end.

**Preuves Cursiv.**

- [src/engines/cursiv/shared/runtime/LexMatch.hpp:53](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/shared/runtime/LexMatch.hpp#L53) — Acceptation et frontières.
- [src/engines/cursiv/shared/runtime/LexWalk.hpp:66](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/shared/runtime/LexWalk.hpp#L66) — Élection des relais.
- [src/engines/cursiv/shared/runtime/LexTape.hpp:299](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/shared/runtime/LexTape.hpp#L299) — Retrait monotone des refusés.
- [src/engines/cursiv/shared/runtime/LexTape.hpp:342](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/shared/runtime/LexTape.hpp#L342) — Modes longer.

**Comparaison.** Cursiv élit la plus longue lecture parmi les terminaux du mode syntaxique courant, puis les tiers et l’ordre. Langium construit un vocabulaire global délégué à Chevrotain, avec mots-clés avant terminaux et LONGER_ALT pour les mots-clés. Les priorités lexicales demandent une adaptation lors d’une migration ; elles sont distinctes des alternatives syntaxiques.

**Preuves de comparaison.**

- [langium/packages/langium/src/parser/lexer.ts:79](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/parser/lexer.ts#L79) — Tokenisation du document entier.
- [langium/packages/langium/src/parser/token-builder.ts:51](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/parser/token-builder.ts#L51) — Ordre global du vocabulaire.
- [langium/packages/langium/src/parser/token-builder.ts:127](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/parser/token-builder.ts#L127) — LONGER_ALT.

**Limites.** Les refus successifs peuvent relire le token pour plusieurs terminaux.

<a id="comparison-lex-024"></a>

### LEX-024 — Tokens virtuels, delimiters et transitions de niveau

**Question.** Comparer indentation, lignes vides et delimiters sur le contrat de tokens virtuels.

**Cursiv.** INDENT consomme la marge et ouvre un niveau ; DEDENT reste sans largeur mais ferme un niveau non muet. Les lignes blanches ne créent pas de transition. Les indents supprimés restent muets pour équilibrer la structure physique ; les inventés se ferment à la marge égale au parent. Les closers saturent la profondeur de délimiteurs à zéro. Les lecteurs des backends demandent les dettes EOF fournies ici.

**Preuves Cursiv.**

- [src/engines/cursiv/shared/runtime/IndentLevel.hpp:168](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/shared/runtime/IndentLevel.hpp#L168) — Synthèse du token.
- [src/engines/cursiv/shared/runtime/IndentLevel.hpp:192](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/shared/runtime/IndentLevel.hpp#L192) — Lignes vides et niveaux muets.
- [src/engines/cursiv/shared/runtime/IndentLevel.hpp:211](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/shared/runtime/IndentLevel.hpp#L211) — Transitions.
- [src/engines/cursiv/shared/runtime/IndentLevel.hpp:254](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/shared/runtime/IndentLevel.hpp#L254) — Réparations.

**Comparaison.** Cursiv compare les préfixes exacts, Langium 4.4.0 leur longueur : tabulation et espace sont égaux en niveau dans le builder Langium standard, mais incompatibles dans Cursiv. Langium utilise PUSH_MODE/POP_MODE pour les délimiteurs et nettoie les couples indent/dedent vides après tokenisation. Cursiv porte l’état dans chaque lecture syntaxique/réparation. Les deux synthétisent les dedents restants ; Langium distingue explicitement full/partial.

**Preuves de comparaison.**

- [langium/packages/langium/src/parser/indentation-aware.ts:147](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/parser/indentation-aware.ts#L147) — Modes des délimiteurs.
- [langium/packages/langium/src/parser/indentation-aware.ts:211](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/parser/indentation-aware.ts#L211) — Longueur du whitespace.
- [langium/packages/langium/src/parser/indentation-aware.ts:356](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/parser/indentation-aware.ts#L356) — Dedents de fin.
- [langium/packages/langium/src/parser/indentation-aware.ts:396](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/parser/indentation-aware.ts#L396) — Full/partial et nettoyage.

**Limites.** Le moment des DEDENT EOF relève du lecteur LL/LR. Les helpers ne vérifient pas l’appariement des espèces de délimiteurs.

<a id="comparison-ll-046"></a>

### LL-046 — Projection multibranche vers les features de complétion

**Question.** Comparer les features attendues au caret et le traitement des alternatives ou conditions indécidables.

**Cursiv.** Les configurations sont projetées par terminal puis par identité de membre/type/narrow ; entered est combiné par accord des lectures. Un membre est pris au site immédiat ou au premier caller qui l’assigne ; une pile inconnue marque floor et impose un replay plus large. Les conditions conservent les survivants par terminal, dédupliqués par configuration exacte avant projection. Les terminaux EOF/indentation et chaînes vides ne deviennent pas des propositions. Les mots littéraux sont réinsérés et relexés pour vérifier frontières et commentaires. Pour les patterns sans graphie, les échantillons bornent la décision ; si aucun échantillon ne les représente, une condition indécidable laisse la proposition ouverte. L’API ne fournit en revanche aucun canal d’annulation ou d’incomplétude de ce travail.

**Preuves Cursiv.**

- [src/engines/cursiv/ll/runtime/ll_caret.hpp:171](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/ll/runtime/ll_caret.hpp#L171) — identité des membres
- [src/engines/cursiv/ll/runtime/ll_caret.hpp:204](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/ll/runtime/ll_caret.hpp#L204) — remontée multibranche
- [src/engines/cursiv/ll/runtime/ll_caret.hpp:688](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/ll/runtime/ll_caret.hpp#L688) — échantillons et inconnu
- [src/engines/cursiv/ll/runtime/ll_caret.hpp:950](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/ll/runtime/ll_caret.hpp#L950) — simulation sans limite
- [src/engines/cursiv/ll/runtime/ll_caret.hpp:1236](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/ll/runtime/ll_caret.hpp#L1236) — condition inconnue conservée
- [src/engines/cursiv/ll/runtime/ll_caret.hpp:1388](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/ll/runtime/ll_caret.hpp#L1388) — projection des survivants
- [src/engines/cursiv/ll/runtime/ll_caret.hpp:1401](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/ll/runtime/ll_caret.hpp#L1401) — floor et élargissement

**Comparaison.** Langium projette des AbstractElement de grammaire accompagnés de type/property et développe alternatives, groupes non ordonnés, cardinalités et appels. Cursiv livre un identifiant terminal et des identités de membre/type, alimentés par une simulation du graphe et une relecture lexicale des candidats. Un provider Langium qui inspecte directement la feature de grammaire doit donc être adapté. Langium conserve également plusieurs piles, mais son calcul ne modélise pas les conditions textuelles propres à Cursiv. Aucun avantage d’annulation n’est attribué à Langium : findNextFeatures est synchrone et le provider de référence reçoit _cancelToken sans le consulter.

**Preuves de comparaison.**

- [langium/packages/langium/src/lsp/completion/follow-element-computation.ts:12](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/completion/follow-element-computation.ts#L12) — contrat NextFeature
- [langium/packages/langium/src/lsp/completion/follow-element-computation.ts:126](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/completion/follow-element-computation.ts#L126) — développement des constructions
- [langium/packages/langium/src/lsp/completion/follow-element-computation.ts:259](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/completion/follow-element-computation.ts#L259) — filtrage des piles
- [langium/packages/langium/src/lsp/completion/completion-provider.ts:162](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/completion/completion-provider.ts#L162) — token non consulté dans le provider

**Limites.** Pas de mesure de latence ni d’exécution. Les échantillons constituent une approximation assumée pour les patterns ; aucun faux négatif concret n’a été établi ici. La présente limite d’annulation ne suppose pas qu’une grammaire ordinaire soit lente.

<a id="comparison-ll-052"></a>

### LL-052 — Arbitrage lexical et préférence du candidat d’épisode

**Question.** Comparer uniquement le choix syntaxique observable en présence de plusieurs lectures ; ne pas présenter les mécanismes LL comme équivalents.

**Cursiv.** L’épisode simple prend la lecture déjà tenue par la descente au premier point ; s’il découvre plusieurs longueurs, rangs lexicaux concurrents ou des traversées de trivia différentes, il repart avec suivi lexical. La voie suivie regroupe les lectures par kernel/contexte de trivia et réunit les alphabets qui partent du même offset. L’ordre des crossings privilégie d’abord la longueur déjà tenue au premier point, puis la longueur décroissante et le rang lexical ; les autres champs stabilisent la sélection. La préférence entre threads compare les histoires lexicales, sauf une divergence dans une décision déclarée scoped où les bras priment. decide fixe une décision une fois et retire les racines rivales ; conclude privilégie une fin prouvée puis les candidats terminés, en conservant des décisions communes pour la réparation en cas d’échec.

**Preuves Cursiv.**

- [src/engines/cursiv/ll/runtime/LlEpisode.hpp:526](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/ll/runtime/LlEpisode.hpp#L526) — décision fixée
- [src/engines/cursiv/ll/runtime/LlEpisode.hpp:631](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/ll/runtime/LlEpisode.hpp#L631) — conclusion
- [src/engines/cursiv/ll/runtime/LlEpisode.hpp:878](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/ll/runtime/LlEpisode.hpp#L878) — passage à la voie suivie
- [src/engines/cursiv/ll/runtime/LlEpisode.hpp:1147](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/ll/runtime/LlEpisode.hpp#L1147) — voie simple
- [src/engines/cursiv/ll/runtime/LlEpisode.hpp:1210](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/ll/runtime/LlEpisode.hpp#L1210) — groupement suivi
- [src/engines/cursiv/ll/runtime/LlEpisode.hpp:1367](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/ll/runtime/LlEpisode.hpp#L1367) — ordre des crossings
- [src/engines/cursiv/ll/runtime/LlEpisodeSim.hpp:144](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/ll/runtime/LlEpisodeSim.hpp#L144) — ordre lexical/bras
- [src/engines/cursiv/ll/runtime/LlEpisodeSim.hpp:684](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/ll/runtime/LlEpisodeSim.hpp#L684) — décision scoped

**Comparaison.** Langium tokenize l’entrée entière avant de la confier à Chevrotain ; alternatives délègue à OR et le wrapper choisit LL(k) si maxLookahead est fourni, sinon LLStar. Cursiv intègre au contraire les lectures lexicales candidates dans ses épisodes de décision et peut départager des découpages selon le contexte syntaxique. Une migration de grammaire doit donc spécifier et vérifier priorités de tokens, découpages et ambiguïtés plutôt que présumer que l’ordre des alternatives produit le même résultat. Les deux architectures ne sont pas présentées comme équivalentes.

**Preuves de comparaison.**

- [langium/packages/langium/src/parser/langium-parser.ts:162](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/parser/langium-parser.ts#L162) — alternative déléguée
- [langium/packages/langium/src/parser/langium-parser.ts:276](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/parser/langium-parser.ts#L276) — lexing avant parsing
- [langium/packages/langium/src/parser/langium-parser.ts:829](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/parser/langium-parser.ts#L829) — stratégies de lookahead

**Limites.** La relation d’ordre des historiques a une réserve séparée en LL-029 ; elle n’est pas reconfirmée comme défaut atteignable ici. Les masques de bras ≥64 rendent une réponse conservatrice0, sans décalage hors borne établi. Analyse statique.

<a id="comparison-lr-040"></a>

### LR-040 — Élection lexicale et refus contextuel

**Question.** Comparer le contrat d’élection visible, sans chercher un moteur LR équivalent dans Langium.

**Cursiv.** L’ordre visible distingue l’allongement du token, puis son poids lexical lorsque les modes permettent de comparer les rangs. Les refus contextuels excluent progressivement les symboles refusés et relisent la même position ; les ex aequo ne sont gardés qu’à bornes et poids égaux. Les comparaisons de piles pour les refus passent par les guards et les réductions, avec dédup des niveaux atteints. LrLexCandidates conserve un résultat maximal par terminal, invalide son index après compaction et applique réservations/refus avant de garder la longueur gagnante. Sa phase de relais peut comparer les candidats par paires et relire leurs préfixes, coût dépendant du nombre de relais mais sans défaut lexical établi.

**Preuves Cursiv.**

- [src/engines/cursiv/lr/runtime/lr_states.hpp:143](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/lr_states.hpp#L143) — Ordre longueur puis poids.
- [src/engines/cursiv/lr/runtime/lr_refusal.hpp:46](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/lr_refusal.hpp#L46) — Marche exacte dans le graphe de piles.
- [src/engines/cursiv/lr/runtime/lr_refusal.hpp:133](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/lr_refusal.hpp#L133) — Exclusion progressive des refus.
- [src/engines/cursiv/lr/runtime/lr_lex_exact.hpp:175](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/lr_lex_exact.hpp#L175) — Collecte des rivaux ex aequo.
- [src/engines/cursiv/lr/runtime/LrLexCandidates.hpp:45](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/LrLexCandidates.hpp#L45) — Réservations, relais et longueur maximale.

**Comparaison.** Le lexer Langium standard tokenize le texte via Chevrotain et renvoie tokens/hidden/errors ; il ne reçoit pas l’état du parseur dans cette API. Cursiv LR peut modifier l’élection selon le contexte de pile, le mode et les refus. Une migration de grammaire doit donc revalider les collisions de tokens ; l’ordre des candidats n’est pas un contrat interchangeable. Des lexers Langium personnalisés/multimodes restent possibles, et ce constat ne signifie pas que tous ses lexers sont identiques au standard.

**Preuves de comparaison.**

- [langium/packages/langium/src/parser/lexer.ts:47](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/parser/lexer.ts#L47) — Interface tokenize sans contexte de pile.
- [langium/packages/langium/src/parser/lexer.ts:68](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/parser/lexer.ts#L68) — Construction du lexer Chevrotain.
- [langium/packages/langium/src/parser/lexer.ts:79](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/parser/lexer.ts#L79) — Tokenisation complète et groupes hidden.

**Limites.** Analyse statique ; aucun scénario exécuté, mesure de latence/mémoire ou atteinte des limites extrêmes affirmée.

<a id="comparison-lr-066"></a>

### LR-066 — Essai de candidats et projection des features

**Question.** Comparer les propositions au caret, indépendamment de la représentation des piles du moteur.

**Cursiv.** Les candidats passent par leurs spellings, l’élection lexicale, les guards/gaps, le maintien de la lecture précédente et les passes de réduction. Une feature est filtrée par sa landing et dédupliquée dans le groupe du terminal ; les différents sites/types ne sont pas écrasés. Les essais restaurent les piles par rollback. Les conditions peuvent être interrogées sur une valeur inconnue puis raffinées par échantillons, avec la limite d’exhaustivité décrite. Les coûts des dédups sont consolidés sous LR-CARET-CACHES-LINEAIRES et l’annulation sous CORE-COMPLETION-BOUNDS.

**Preuves Cursiv.**

- [src/engines/cursiv/lr/runtime/LrCaretTable.cpp:563](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/LrCaretTable.cpp#L563) — Essai de candidat et rollback.
- [src/engines/cursiv/lr/runtime/LrCaretTable.cpp:2250](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/LrCaretTable.cpp#L2250) — Validation des passes dans les autres modes.
- [src/engines/cursiv/lr/runtime/LrCaretTable.cpp:2443](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/LrCaretTable.cpp#L2443) — Projection des sites et filtre landing.
- [src/engines/cursiv/lr/runtime/LrCaretTable.cpp:2467](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/engines/cursiv/lr/runtime/LrCaretTable.cpp#L2467) — Raffinement par échantillonnage.

**Comparaison.** Langium calcule des NextFeature structurelles (feature/type/property) à partir des cardinalités, appels de règles et tokens non consommés. Cursiv ajoute la vérification lexicale contextuelle, les landings et conditions au caret ; ce comportement plus précis localement n’implique pas une exhaustivité des terminaux sans spelling. Le module de calcul structurel Langium lu n’offre pas non plus de token d’annulation dans ses signatures ; ne pas présenter ce seul écart comme une garantie comparative globale.

**Preuves de comparaison.**

- [langium/packages/langium/src/lsp/completion/follow-element-computation.ts:34](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/completion/follow-element-computation.ts#L34) — Interprétation des tokens puis calcul des features suivantes.
- [langium/packages/langium/src/lsp/completion/follow-element-computation.ts:126](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/completion/follow-element-computation.ts#L126) — Descente structurelle groupes/alternatives/assignments/appels.
- [langium/packages/langium/src/lsp/completion/follow-element-computation.ts:317](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/completion/follow-element-computation.ts#L317) — Match des tokens par keyword ou regex de terminal.

**Limites.** Analyse statique uniquement ; aucune requête de complétion ni grammaire de démonstration exécutée. Les coûts décrits sont structurels, pas mesurés.

<a id="comparison-lsp-008"></a>

### LSP-008 — Adaptation des résultats aux capacités client

**Question.** Comparer les variantes de réponse autorisées par les capacités réellement négociées.

**Cursiv.** Les quatre navigations utilisent le flag correspondant à leur capacité, et la conversion en Location conserve targetUri avec targetSelectionRange. L’aplatissement des symboles est itératif, conserve range/kind/tags et reconstruit containerName via un indice de parent stable malgré les réallocations du vecteur. Aucun emplacement erroné identifié.

**Preuves Cursiv.**

- [src/lsp/cursiv/runtime/LanguageServerRuntime.cpp:325](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/LanguageServerRuntime.cpp#L325) — Flags séparés par méthode.
- [src/lsp/cursiv/runtime/LanguageServerRuntime.cpp:372](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/LanguageServerRuntime.cpp#L372) — Choix LocationLink/Location.
- [src/lsp/cursiv/runtime/LanguageServerRuntime.cpp:391](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/LanguageServerRuntime.cpp#L391) — Aplatissement parent avant enfants.

**Comparaison.** Cursiv adapte explicitement les résultats aux capacités linkSupport et hierarchicalDocumentSymbolSupport. Les wrappers Langium cités délèguent directement aux providers et n’appliquent pas cette conversion centrale ; une langue Langium doit donc gérer ses variantes dans le provider ou la connexion.

**Preuves de comparaison.**

- [langium/packages/langium/src/lsp/language-server.ts:422](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/language-server.ts#L422) — DocumentSymbol directement retourné par le provider.
- [langium/packages/langium/src/lsp/language-server.ts:430](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/language-server.ts#L430) — Definition directement retournée.

**Limites.** Analyse statique des sources et contrats ; aucune exécution du projet.

<a id="comparison-lsp-010"></a>

### LSP-010 — Services installés et capacités annoncées

**Question.** Comparer disponibilité par défaut, service optionnel et capacité annoncée pour chaque provider, sans assimiler interface présente et comportement fourni.

**Cursiv.** Les slots LSP de langue sont optionnels et initialize ne publie leurs capacités qu’après dynamic_cast et présence du provider. Les triggers sont fusionnés entre langues ; le conteneur typé visite les services core puis seulement les slots propres au niveau LSP. Les valeurs requises shared sont installées avant usage. Aucun provider absent annoncé par erreur dans les chemins lus.

**Preuves Cursiv.**

- [src/lsp/cursiv/services/LspServices.cpp:35](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/services/LspServices.cpp#L35) — Slots optionnels et installation des défauts.
- [src/lsp/cursiv/services/SharedLspServices.cpp:30](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/services/SharedLspServices.cpp#L30) — Visite des slots partagés, obligatoires et optionnels.
- [src/lsp/cursiv/services/LspServicesFor.hpp:20](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/services/LspServicesFor.hpp#L20) — Typage du diamant et visite unique.
- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:292](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L292) — Capacités obtenues depuis les providers.

**Comparaison.** Les deux projets conditionnent les capacités à la présence de services. Cursiv fournit par défaut outline/navigation/références/rename/highlight/folding/selection/hover/completion/code actions ; formatter, semantic tokens, hiérarchies et autres services métier restent à installer. Langium emploie aussi des services optionnels, mais ses options semantic/completion peuvent annoncer des extensions supplémentaires étudiées dans leurs scopes dédiés.

**Preuves de comparaison.**

- [langium/packages/langium/src/lsp/language-server.ts:97](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/language-server.ts#L97) — Recherche des providers installés.
- [langium/packages/langium/src/lsp/language-server.ts:102](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/language-server.ts#L102) — Construction des capacités.
- [langium/packages/langium/src/lsp/default-lsp-module.ts:35](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/default-lsp-module.ts#L35) — Assemblage des providers par défaut.

**Limites.** Analyse statique des sources et contrats ; aucune exécution du projet.

<a id="comparison-lsp-011"></a>

### LSP-011 — Ouverture d’un document

**Question.** Comparer quelle source de texte et quelle version font autorité à l’ouverture.

**Cursiv.** didOpen donne priorité au texte, à la langue et à la version du client. Un document déjà indexé conserve son identité ; son parse n’est réattaché que si texte/langue changent. Sinon seule la version progresse et les diagnostics peuvent être republiés. Le flag isOpen est fixé dans la même action d’écriture. Aucune incohérence trouvée entre ouverture sur scan et ouverture nouvelle.

**Preuves Cursiv.**

- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:551](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L551) — Mutation sous écriture.
- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:555](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L555) — Comparaison texte et langue.
- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:571](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L571) — Primauté de la version client.

**Comparaison.** Langium reçoit le buffer via TextDocuments et son factory privilégie ce buffer lors d’un update ; Cursiv possède directement le texte de didOpen. Tous deux évitent de reparsing un texte identique. La version est un champ Cursiv mis à jour explicitement ; chez Langium elle suit le TextDocument externe.

**Preuves de comparaison.**

- [langium/packages/langium/src/lsp/language-server.ts:325](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/language-server.ts#L325) — Abonnements TextDocuments.
- [langium/packages/langium/src/lsp/document-update-handler.ts:133](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/document-update-handler.ts#L133) — L’événement de contenu déclenche l’update.
- [langium/packages/langium/src/workspace/documents.ts:279](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/workspace/documents.ts#L279) — Le factory utilise le TextDocument ouvert.

**Limites.** Analyse statique des sources et contrats ; aucune exécution du projet.

<a id="comparison-lsp-012"></a>

### LSP-012 — Modifications et versions

**Question.** Comparer admission des versions et visibilité des nouvelles données lors de la requête suivante.

**Cursiv.** Le serveur écarte une version inférieure, accepte une version égale et applique les changements dans l’ordre à une copie avant remplacement. Les messages vides ou donnant le même texte ne reconstruisent pas le parse mais actualisent la version. Une URI absente est ignorée ; le code ne contrôle pas isOpen pour une URI déjà scannée. Cela décrit une tolérance à des messages hors cycle, pas un défaut démontré sur un client LSP conforme.

**Preuves Cursiv.**

- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:585](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L585) — Admission et traitement atomique du nouveau texte.
- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:618](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L618) — Version et republication des diagnostics.

**Comparaison.** Dans Langium, l’admission des versions et l’application des edits sont déléguées au service TextDocuments de vscode-languageserver, hors des sources Langium ciblées ; la mise à jour Langium reçoit ensuite le buffer courant. Cursiv assure lui-même le rejet des versions inférieures et la barrière réseau. Aucune équivalence de rejet des versions égales n’est affirmée sans audit de cette dépendance Langium.

**Preuves de comparaison.**

- [langium/packages/langium/src/lsp/language-server.ts:325](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/language-server.ts#L325) — La synchronisation dépend de TextDocuments.
- [langium/packages/langium/src/lsp/document-update-handler.ts:133](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/document-update-handler.ts#L133) — Le handler reçoit un document déjà mis à jour.
- [langium/packages/langium/src/workspace/documents.ts:282](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/workspace/documents.ts#L282) — Texte/version issus du buffer client.

**Limites.** Analyse statique des sources et contrats ; aucune exécution du projet.

<a id="comparison-lsp-013"></a>

### LSP-013 — Fermeture et retour au contenu persistant

**Question.** Comparer le retour à l’état persistant et la conservation des documents fermés.

**Cursiv.** didClose relâche isOpen sous le verrou d’écriture, puis reconstruit depuis le contenu persistant pour un fichier existant ou un schéma de contenu annoncé. Un buffer sans source persistante est supprimé et suit le chemin d’effacement des diagnostics. Les URI inconnues déclenchent seulement un build vide. Aucun défaut d’ordre identifié.

**Preuves Cursiv.**

- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:632](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L632) — Sélection reconstruction/suppression.
- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:1396](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L1396) — Effacement des annonces périmées.

**Comparaison.** Cursiv impose immédiatement le retour au contenu sauvegardé ou le retrait du buffer sans source. Le DefaultDocumentUpdateHandler Langium n’implémente pas didCloseDocument ; un prochain update du factory utilisera le disque si TextDocuments ne détient plus le buffer. Le comportement par défaut à la fermeture est donc plus actif dans Cursiv, et un portage doit vérifier ses attentes sur les buffers non sauvegardés.

**Preuves de comparaison.**

- [langium/packages/langium/src/lsp/document-update-handler.ts:58](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/document-update-handler.ts#L58) — Hook de fermeture optionnel.
- [langium/packages/langium/src/lsp/document-update-handler.ts:78](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/document-update-handler.ts#L78) — Implémentation par défaut sans hook de fermeture.
- [langium/packages/langium/src/workspace/documents.ts:314](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/workspace/documents.ts#L314) — Repli disque lors de la prochaine mise à jour.

**Limites.** Analyse statique des sources et contrats ; aucune exécution du projet.

<a id="comparison-lsp-016"></a>

### LSP-016 — Acquisition et cache de configuration

**Question.** Comparer sections, valeurs de repli, portées URI et disponibilité hors d’une session LSP.

**Cursiv.** Le cache distingue section inconnue et valeur null, remplace une section entière et protège ses lectures par mutex. Une réponse ancienne est rejetée par génération ; une valeur poussée après le début du pull prime sur sa réponse. Les listeners sont appelés hors mutex. getConfiguration reste non bloquant : le lecteur doit accepter nullopt avant le premier succès. La conservation inter-session relève de LSP-002.

**Preuves Cursiv.**

- [src/lsp/cursiv/workspace/DefaultConfigurationProvider.cpp:140](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/workspace/DefaultConfigurationProvider.cpp#L140) — Génération, révision et valeurs prioritaires.
- [src/lsp/cursiv/workspace/DefaultConfigurationProvider.cpp:195](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/workspace/DefaultConfigurationProvider.cpp#L195) — Push ou nouveau pull.
- [src/lsp/cursiv/workspace/DefaultConfigurationProvider.cpp:213](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/workspace/DefaultConfigurationProvider.cpp#L213) — Lecture synchrone non bloquante.

**Comparaison.** Les deux caches utilisent les languageIds et la portée workspace sans scopeUri ; l’absence de configuration par document n’est donc pas un retard sur le défaut Langium. Langium getConfiguration attend ready et expose ConfigurationProvider dans les services core ; Cursiv renvoie immédiatement optional et place cette API dans la couche LSP. Cursiv traite aussi null/objet vide comme une demande de rafraîchissement, là où Langium ignore null et n’itère aucune clé pour {}.

**Preuves de comparaison.**

- [langium/packages/langium/src/workspace/configuration.ts:122](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/workspace/configuration.ts#L122) — Sections et portée par défaut.
- [langium/packages/langium/src/workspace/configuration.ts:143](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/workspace/configuration.ts#L143) — Traitement des changements.
- [langium/packages/langium/src/workspace/configuration.ts:163](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/workspace/configuration.ts#L163) — Lecture asynchrone attendant ready.

**Limites.** Analyse statique des sources et contrats ; aucune exécution du projet.

<a id="comparison-lsp-017"></a>

### LSP-017 — Configuration et catégories de validation

**Question.** Comparer quand un changement de préférences impose de recalculer les diagnostics et comment le langage personnalise ce lien.

**Cursiv.** La configuration validation accepte une liste ou un objet categories, résout les catégories identitaires connues et déduplique. Une liste vide ou ne contenant que des noms inconnus signifie explicitement toutes les catégories : le comportement est documenté, pas un bug de parsing. Le LspDocumentBuilder calcule les options par langue et un changement de section remet les documents concernés au plus à IndexedReferences, puis les revalide.

**Preuves Cursiv.**

- [src/lsp/cursiv/workspace/ConfigurationProvider.hpp:32](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/workspace/ConfigurationProvider.hpp#L32) — Sémantique explicite de la liste vide.
- [src/lsp/cursiv/workspace/LspDocumentBuilder.cpp:43](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/workspace/LspDocumentBuilder.cpp#L43) — Choix des options.
- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:747](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L747) — Reset et rebuild de la langue concernée.

**Comparaison.** Langium fournit un événement de configuration mais ne lie pas par défaut une clé validation aux options du builder dans ce provider. Cursiv apporte ce lien et la revalidation automatique. Une langue ayant sa propre syntaxe de préférences doit adapter configuredBuildOptions et/ou le listener ; interpréter false ou [] comme désactivation ne correspond pas au contrat Cursiv.

**Preuves de comparaison.**

- [langium/packages/langium/src/workspace/configuration.ts:143](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/workspace/configuration.ts#L143) — Mise à jour et événement, sans revalidation intégrée.
- [langium/packages/langium/src/workspace/configuration.ts:176](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/workspace/configuration.ts#L176) — Point d’abonnement public.

**Limites.** Analyse statique des sources et contrats ; aucune exécution du projet.

<a id="comparison-lsp-018"></a>

### LSP-018 — Hooks d’opérations sur fichiers

**Question.** Comparer hooks will/did et filtrage des opérations proposés aux langages.

**Cursiv.** Les six hooks sont facultatifs par leur corps par défaut, tandis que les options stables sélectionnent les routes enregistrées pour toute la session. Les did s’exécutent sans verrou et laissent l’application écrire ; les will calculent un edit sous verrou de lecture et retournent null lorsqu’aucun edit n’est proposé. Le token protège l’attente mais ne traverse pas le hook, ce qui rejoint la signature de référence Langium.

**Preuves Cursiv.**

- [src/lsp/cursiv/workspace/FileOperationHandler.hpp:17](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/workspace/FileOperationHandler.hpp#L17) — Options stables par session.
- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:768](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L768) — did sans lock.
- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:788](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L788) — will sous read.
- [src/lsp/cursiv/runtime/LanguageServerRuntime.cpp:782](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/LanguageServerRuntime.cpp#L782) — Routes choisies à partir des options.

**Comparaison.** Les deux API ont les six événements et la même responsabilité applicative sur WorkspaceEdit/filtres. Langium enregistre une méthode lorsqu’elle est implémentée, avec MaybePromise pour will ; Cursiv enregistre selon l’option annoncée et impose une réponse synchrone sous read. Aucun des deux hooks comparés ne reçoit directement un token d’annulation.

**Preuves de comparaison.**

- [langium/packages/langium/src/lsp/file-operation-handler.ts:14](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/file-operation-handler.ts#L14) — Hooks optionnels.
- [langium/packages/langium/src/lsp/language-server.ts:351](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/language-server.ts#L351) — Enregistrement selon présence des hooks.

**Limites.** Analyse statique des sources et contrats ; aucune exécution du projet.

<a id="comparison-lsp-019"></a>

### LSP-019 — Prérequis de phase et langue du document

**Question.** Comparer la déclaration d’un prérequis par service et les états imposés par le routeur.

**Cursiv.** Le routeur attend le scan puis la phase document ou workspace sans garder le read lock ; il retrouve le document, revérifie son état et utilise la langue liée par la factory. Les lectures workspace sont Normal afin d’éviter de voir un index en reconstruction ; les lectures locales sont Immediate après leur prérequis. Les wrappers déclarent leurs phases de façon cohérente avec leurs besoins, mais les phases ne sont pas configurables par provider dans l’API d’assemblage.

**Preuves Cursiv.**

- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:853](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L853) — Attente du scan.
- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:866](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L866) — ID acquis avant attente.
- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:881](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L881) — Revérification et choix de langue.
- [src/lsp/cursiv/runtime/ServiceRequirement.hpp:11](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/ServiceRequirement.hpp#L11) — Distinction document/workspace.

**Comparaison.** Langium déclare un ServiceRequirement lors de la construction des handlers et accepte un document/workspace state ; Cursiv possède le même concept mais le fixe dans les méthodes serveur. Cursiv revérifie l’état après acquisition et route via document.language ; Langium récupère les services par URI et peut créer le document lors du read.

**Preuves de comparaison.**

- [langium/packages/langium/src/lsp/language-server.ts:786](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/language-server.ts#L786) — API de handler paramétrable.
- [langium/packages/langium/src/lsp/language-server.ts:825](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/language-server.ts#L825) — Attente document/workspace.
- [langium/packages/langium/src/lsp/language-server.ts:804](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/language-server.ts#L804) — Services par URI et getOrCreateDocument.

**Limites.** Analyse statique des sources et contrats ; aucune exécution du projet.

<a id="comparison-lsp-020"></a>

### LSP-020 — Publication des instantanés de diagnostics

**Question.** Comparer les versions et événements auxquels sont rattachées publication et disparition des diagnostics.

**Cursiv.** Les annonces suivent la paire buildRevision/nombre de diagnostics. Les resets du builder vident les diagnostics ; Linked publie alors le vide et Validated publie le nouveau bloc. Une simple nouvelle version republie les erreurs sans reparsing. Les relatedInformation sont converties avec le TextDocument cible, pas celui du diagnostic principal. Aucune suppression incorrecte trouvée dans le cycle normal du builder ; une mutation manuelle des diagnostics à révision/nombre constants sortirait de cette convention.

**Preuves Cursiv.**

- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:1386](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L1386) — Phases d’effacement/publication.
- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:1423](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L1423) — Déduplication des annonces.
- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:1488](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L1488) — Conversion des documents liés.
- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:1512](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L1512) — Suppression sans version.

**Comparaison.** Langium publie au passage document Validated et efface les URI supprimées à onUpdate, sans version dans addDiagnosticsHandler. Cursiv publie une version, déduplique les blocs et efface les anciens diagnostics dès Linked. Les différences affectent le moment où les soulignements disparaissent et la capacité du client à rejeter un snapshot ancien.

**Preuves de comparaison.**

- [langium/packages/langium/src/lsp/language-server.ts:376](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/language-server.ts#L376) — Suppression et publication de diagnostics Langium.

**Limites.** Analyse statique des sources et contrats ; aucune exécution du projet.

<a id="comparison-lsp-023"></a>

### LSP-023 — Contexte et reprise au curseur

**Question.** Comparer la transmission de triggerKind/triggerCharacter jusqu’aux extensions et la construction du contexte au caret.

**Cursiv.** Le contexte distingue remplacement du token actuel, insertion après token et reprise d’une règle de données. Les offsets internes sont calculés via TextDocument ; les insert/replace ranges sont limités à la ligne du caret. La complétion est exclue strictement à l’intérieur des trivia/recovered leaves. Les hooks disposent de l’identifiant et du token d’annulation, mais jamais du motif de la requête LSP.

**Preuves Cursiv.**

- [src/lsp/cursiv/completion/DefaultCompletionProvider.cpp:148](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/completion/DefaultCompletionProvider.cpp#L148) — Construction des contextes et plages.
- [src/lsp/cursiv/completion/DefaultCompletionProvider.cpp:242](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/completion/DefaultCompletionProvider.cpp#L242) — Reprise au début de règle de données.
- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:1126](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L1126) — La position seule franchit la frontière provider.

**Comparaison.** Langium reçoit les CompletionParams complets dans getCompletion, bien que son implémentation par défaut ne passe ensuite que position à buildContexts. Cursiv rend impossible cette personnalisation au niveau du provider. Les deux reprennent les règles de données et le token précédent ; Cursiv s’appuie sur nextFeaturesAt du CST et Langium reparse le préfixe avec CompletionParser.

**Preuves de comparaison.**

- [langium/packages/langium/src/lsp/completion/completion-provider.ts:162](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/completion/completion-provider.ts#L162) — Paramètres et contextes.
- [langium/packages/langium/src/lsp/completion/completion-provider.ts:210](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/completion/completion-provider.ts#L210) — Reparse du préfixe.
- [langium/packages/langium/src/lsp/completion/completion-provider.ts:262](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/completion/completion-provider.ts#L262) — Contextes au caret.

**Limites.** Lecture statique de l’implémentation, des contrats et des callers ; aucun test ou code projet exécuté.

<a id="comparison-lsp-024"></a>

### LSP-024 — Features de référence et candidats

**Question.** Comparer filtre de type, visibilité et déduplication des candidats de référence.

**Cursiv.** referenceInfoOf part de la propriété grammaticale et du type déclaré par NextFeature ; le narrow du site est conservé. Un type nouvellement entré reçoit un nœud synthétique, temporairement parenté au contexte. Les candidats sont obtenus via visitScopeEntries, filtrés avant calcul de documentation et dédupliqués par nom/type. Sans nœud de contexte, aucune portée globale arbitraire n’est fabriquée. Aucun contournement de filtre de portée retenu.

**Preuves Cursiv.**

- [src/lsp/cursiv/completion/DefaultCompletionProvider.cpp:356](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/completion/DefaultCompletionProvider.cpp#L356) — Construction ReferenceInfo, type/narrow et nœud temporaire.
- [src/lsp/cursiv/completion/DefaultCompletionProvider.cpp:417](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/completion/DefaultCompletionProvider.cpp#L417) — Filtre fuzzy et dédup avant génération.
- [src/lsp/cursiv/completion/DefaultCompletionProvider.cpp:444](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/completion/DefaultCompletionProvider.cpp#L444) — Délégation ScopeProvider.

**Comparaison.** Les deux délèguent aux portées et fabriquent un élément synthétique lorsqu’une règle n’existe pas encore. Langium renseigne aussi $containerProperty sur cet objet ; Cursiv ne porte pas cette donnée dans AstNode/NextFeature et un scope qui en dépend doit adapter son contexte. Cursiv déduplique nom/type avant documentation et limite la collecte autour de 1000 items ; Langium déduplique les items après création sans cette borne.

**Preuves de comparaison.**

- [langium/packages/langium/src/lsp/completion/completion-provider.ts:446](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/completion/completion-provider.ts#L446) — Élément synthétique avec containerProperty.
- [langium/packages/langium/src/lsp/completion/completion-provider.ts:497](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/completion/completion-provider.ts#L497) — Candidats de ScopeProvider.
- [langium/packages/langium/src/lsp/completion/completion-provider.ts:206](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/completion/completion-provider.ts#L206) — Déduplication finale.

**Limites.** Lecture statique de l’implémentation, des contrats et des callers ; aucun test ou code projet exécuté.

<a id="comparison-lsp-025"></a>

### LSP-025 — Items, documentation et modifications

**Question.** Comparer insert/replace ranges, personnalisation des items, documentation et résolution différée.

**Cursiv.** fillCompletionItem respecte un textEdit explicite, sinon choisit insertText ou label, applique le matcher et génère l’edit. insertReplaceSupport décide de l’union InsertReplaceEdit/TextEdit ; la forme simple n’efface que le préfixe jusqu’au caret. La déduplication calcule les clés en vues avant de déplacer les items, évitant les vues invalidées. Documentation des références obtenue dans leur langue d’origine. Aucun défaut de durée de vie ou de plage identifié.

**Preuves Cursiv.**

- [src/lsp/cursiv/completion/DefaultCompletionProvider.cpp:305](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/completion/DefaultCompletionProvider.cpp#L305) — Marquage des doublons avant compaction.
- [src/lsp/cursiv/completion/DefaultCompletionProvider.cpp:450](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/completion/DefaultCompletionProvider.cpp#L450) — Construction des références et documentation.
- [src/lsp/cursiv/completion/DefaultCompletionProvider.cpp:525](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/completion/DefaultCompletionProvider.cpp#L525) — Remplissage et edits.
- [src/lsp/cursiv/completion/DefaultCompletionProvider.cpp:550](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/completion/DefaultCompletionProvider.cpp#L550) — Négociation insert/replace.

**Comparaison.** La clé kind/label/detail, le repli de TextEdit au préfixe et le calcul immédiat de documentation correspondent à Langium. Ni l’interface Langium comparée ni celle de Cursiv ne fournit completionItem/resolve par défaut : ce n’est pas une fonctionnalité uniquement manquante dans Cursiv. Cursiv limite explicitement les plages à la ligne du caret et lit la documentation du langage déclarant pour les candidats interlangues.

**Preuves de comparaison.**

- [langium/packages/langium/src/lsp/completion/completion-provider.ts:206](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/completion/completion-provider.ts#L206) — Clé de dédup.
- [langium/packages/langium/src/lsp/completion/completion-provider.ts:512](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/completion/completion-provider.ts#L512) — Documentation immédiate.
- [langium/packages/langium/src/lsp/completion/completion-provider.ts:607](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/completion/completion-provider.ts#L607) — Choix insert/replace.

**Limites.** Lecture statique de l’implémentation, des contrats et des callers ; aucun test ou code projet exécuté.

<a id="comparison-lsp-026"></a>

### LSP-026 — Correspondance approximative

**Question.** Comparer le contrat de correspondance approximative utilisé par les listes de propositions.

**Cursiv.** Le matcher consomme des unités UTF-16 reconstituées depuis UTF-8, replie seulement les lettres ASCII et cherche une ancre au début, sur transition camelCase ou après underscore. Après la première ancre, les autres lettres peuvent apparaître plus loin. Une requête vide réussit ; les curseurs progressent même sur octet invalide. Aucun écart d’algorithme constaté avec le matcher de référence.

**Preuves Cursiv.**

- [src/lsp/cursiv/completion/DefaultFuzzyMatcher.cpp:18](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/completion/DefaultFuzzyMatcher.cpp#L18) — Curseur UTF-16.
- [src/lsp/cursiv/completion/DefaultFuzzyMatcher.cpp:50](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/completion/DefaultFuzzyMatcher.cpp#L50) — Transitions et boucle de match.

**Comparaison.** L’algorithme Langium utilise exactement l’ancrage initial puis la sous-séquence, avec casse ASCII et transitions camelCase/snake_case. L’absence de repli Unicode complet est donc commune à ces matchers ; elle ne doit pas être confondue avec la différence de lower_case des scopes traitée en REF-005.

**Preuves de comparaison.**

- [langium/packages/langium/src/lsp/fuzzy-matcher.ts:26](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/fuzzy-matcher.ts#L26) — Boucle et maintien de l’ancre.
- [langium/packages/langium/src/lsp/fuzzy-matcher.ts:54](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/fuzzy-matcher.ts#L54) — Transitions et repli ASCII.

**Limites.** Lecture statique de l’implémentation, des contrats et des callers ; aucun test ou code projet exécuté.

<a id="comparison-lsp-027"></a>

### LSP-027 — Définition

**Question.** Comparer les plages source/cible et les références admissibles.

**Cursiv.** La résolution du caret est centralisée dans References. Chaque cible reçoit une LocationLink dont originSelectionRange vient du document source, tandis que targetRange et targetSelectionRange sont converties avec son propre document et son NameProvider. Les nœuds sans CST utilisent le nom si disponible et les cibles sans document sont ignorées. La liste couvre les MultiRef et le nom d’une déclaration ; aucune confusion interdocument identifiée.

**Preuves Cursiv.**

- [src/lsp/cursiv/navigation/DefaultDefinitionProvider.cpp:37](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/navigation/DefaultDefinitionProvider.cpp#L37) — Résolution commune du caret.
- [src/lsp/cursiv/navigation/DefaultDefinitionProvider.cpp:57](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/navigation/DefaultDefinitionProvider.cpp#L57) — Langue et positions de la cible.
- [src/lsp/cursiv/navigation/NavigationProviderSupport.cpp:22](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/navigation/NavigationProviderSupport.cpp#L22) — Helper de collecte par déclaration.

**Comparaison.** Les deux providers distinguent source, déclaration entière et sélection du nom, et produisent plusieurs liens. Langium remonte explicitement au datatype source node ; Cursiv reçoit déjà la plage sémantique de References. Cursiv sélectionne le NameProvider du document cible, ce qui rend explicite la prise en charge interlangue.

**Preuves de comparaison.**

- [langium/packages/langium/src/lsp/definition-provider.ts:68](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/definition-provider.ts#L68) — Plages de LocationLink.
- [langium/packages/langium/src/lsp/definition-provider.ts:81](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/definition-provider.ts#L81) — Cibles et datatype source.

**Limites.** Lecture statique de l’implémentation, des contrats et des callers ; aucun test ou code projet exécuté.

<a id="comparison-lsp-028"></a>

### LSP-028 — Références et occurrences locales

**Question.** Comparer inclusion de la déclaration et périmètre documentaire/workspace des usages.

**Cursiv.** La recherche rassemble toutes les cibles du caret, conserve includeDeclaration puis trie/déduplique par document et plage complète. reference_location exige un document source détenu pour convertir ses offsets. Le highlight limite FindReferencesOptions au document demandé et inclut la déclaration seulement lorsqu’elle y réside. Le provider retourne une plage sans kind Read/Write, laissée à la personnalisation. Aucun mélange de coordonnées ou doublon de plage trouvé.

**Preuves Cursiv.**

- [src/lsp/cursiv/navigation/DefaultReferencesProvider.cpp:31](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/navigation/DefaultReferencesProvider.cpp#L31) — Options et cibles.
- [src/lsp/cursiv/navigation/NavigationProviderSupport.cpp:43](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/navigation/NavigationProviderSupport.cpp#L43) — Tri et dédup par begin/end.
- [src/lsp/cursiv/navigation/DefaultDocumentHighlightProvider.cpp:31](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/navigation/DefaultDocumentHighlightProvider.cpp#L31) — Portée locale.
- [src/lsp/cursiv/navigation/ReferenceLocation.cpp:17](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/navigation/ReferenceLocation.cpp#L17) — Conversion depuis document source.

**Comparaison.** Langium explore également chaque cible et respecte includeDeclaration ; ses ReferenceDescription portent déjà des ranges et URI, alors que Cursiv a besoin du TextDocument source vivant. Cursiv déduplique les occurrences communes à plusieurs cibles au niveau du helper, tandis que le wrapper Langium comparé pousse les résultats directement.

**Preuves de comparaison.**

- [langium/packages/langium/src/lsp/references-provider.ts:62](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/references-provider.ts#L62) — Boucle par cible et options.
- [langium/packages/langium/src/lsp/document-highlight-provider.ts:57](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/document-highlight-provider.ts#L57) — Occurrences du document et déclaration.

**Limites.** Lecture statique de l’implémentation, des contrats et des callers ; aucun test ou code projet exécuté.

<a id="comparison-lsp-029"></a>

### LSP-029 — Préparation et éditions de renommage

**Question.** Comparer préconditions, identités cibles et modifications produites pour une même demande de renommage.

**Cursiv.** prepareRename admet une référence non résolue si sa plage est connue, ce qui autorise la correction du nom isolé. rename fusionne toutes les cibles et références, groupe les substitutions par document puis reparse chaque texte proposé avec la langue correspondante. Il rejette les recouvrements, noms vides, dégradation du parse et changement de rôle grammatical. Il ne vérifie pas la capture d’un autre symbole après liaison : cette politique reste à la langue.

**Preuves Cursiv.**

- [src/lsp/cursiv/navigation/DefaultRenameProvider.cpp:80](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/navigation/DefaultRenameProvider.cpp#L80) — Préparation et nom non résolu.
- [src/lsp/cursiv/navigation/DefaultRenameProvider.cpp:94](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/navigation/DefaultRenameProvider.cpp#L94) — Reparse et comparaison de rôle.
- [src/lsp/cursiv/navigation/DefaultRenameProvider.cpp:189](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/navigation/DefaultRenameProvider.cpp#L189) — Fusion et validation des documents.

**Comparaison.** Le défaut Langium ne reparse pas newName et ne renomme pas une référence sans cible ; il part d’une cible et suppose findReferences couvrir les autres. Cursiv apporte une validation syntaxique interlangue et un renommage isolé pour corriger une référence non résolue, au coût d’un parse complet par document sous le read lock. Aucun des deux défauts comparés ne prouve que la résolution sémantique reste inchangée après renommage.

**Preuves de comparaison.**

- [langium/packages/langium/src/lsp/rename-provider.ts:63](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/rename-provider.ts#L63) — Remplacements sans validation syntaxique.
- [langium/packages/langium/src/lsp/rename-provider.ts:95](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/rename-provider.ts#L95) — Conditions de préparation.

**Limites.** Lecture statique de l’implémentation, des contrats et des callers ; aucun test ou code projet exécuté.

<a id="comparison-lsp-030"></a>

### LSP-030 — Hiérarchie de symboles du document

**Question.** Comparer hiérarchie, plages et personnalisation des symboles documentaires.

**Cursiv.** collect_named_nodes identifie les nœuds par identité même lorsque plusieurs partagent un record CST, puis mémoïse le plus proche ancêtre nommé. Les symboles sont assemblés en postordre, range depuis la déclaration et selectionRange depuis le nom. L’entrée inclut les records externes de son span. Les nœuds sans nom s’effacent de la hiérarchie mais leurs descendants restent rattachés au bon ancêtre. Aucun défaut trouvé sous le contrat postordre de ParseResult.nodes.

**Preuves Cursiv.**

- [src/lsp/cursiv/symbols/NamedNodes.cpp:24](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/symbols/NamedNodes.cpp#L24) — Correspondance identité/record.
- [src/lsp/cursiv/symbols/NamedNodes.cpp:47](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/symbols/NamedNodes.cpp#L47) — Ancêtre nommé mémorisé.
- [src/lsp/cursiv/symbols/DefaultDocumentSymbolProvider.cpp:47](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/symbols/DefaultDocumentSymbolProvider.cpp#L47) — Plages et assemblage.
- [src/lsp/cursiv/symbols/DefaultNodeKindProvider.cpp:9](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/symbols/DefaultNodeKindProvider.cpp#L9) — Genres par défaut neutres.

**Comparaison.** Langium traverse récursivement les enfants et expose getSymbol/getChildSymbols ; Cursiv construit une table globale puis assemble les parents et expose createSymbol. Le résultat conserve la même dissolution des nœuds anonymes et la distinction range/selectionRange, mais personnaliser l’élagage d’une branche demande une surcharge plus large de getSymbols dans Cursiv.

**Preuves de comparaison.**

- [langium/packages/langium/src/lsp/document-symbol-provider.ts:49](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/document-symbol-provider.ts#L49) — Nœuds anonymes et création du symbole.
- [langium/packages/langium/src/lsp/document-symbol-provider.ts:69](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/document-symbol-provider.ts#L69) — Hook de parcours des enfants.

**Limites.** Lecture statique de l’implémentation, des contrats et des callers ; aucun test ou code projet exécuté.

<a id="comparison-lsp-031"></a>

### LSP-031 — Recherche et résolution de symboles du workspace

**Question.** Comparer recherche partielle et résolution différée des descriptions indexées.

**Cursiv.** Le provider workspace parcourt les exports, vérifie l’annulation à chaque entrée, applique le matcher puis produit les positions avec le NameProvider du document cible. La résolution différée est disponible avec un indicateur explicite resolvesSymbols et le serveur attend l’index workspace avant recherche/résolution. Aucun symbole ne repose sur les coordonnées du document demandeur. L’énumération reste exhaustive et non plafonnée.

**Preuves Cursiv.**

- [src/lsp/cursiv/symbols/DefaultWorkspaceSymbolProvider.cpp:27](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/symbols/DefaultWorkspaceSymbolProvider.cpp#L27) — Recherche des exports.
- [src/lsp/cursiv/symbols/DefaultWorkspaceSymbolProvider.cpp:44](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/symbols/DefaultWorkspaceSymbolProvider.cpp#L44) — Position depuis le document cible.
- [src/lsp/cursiv/symbols/WorkspaceSymbolProvider.hpp:26](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/symbols/WorkspaceSymbolProvider.hpp#L26) — Opt-in de résolution.
- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:1321](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L1321) — Routage shared après index.

**Comparaison.** Langium peut fabriquer le résultat depuis nameSegment sans AST chargé ; Cursiv exige document et node pour lire le nom. Les deux exposent la résolution optionnelle, mais Cursiv demande aussi resolvesSymbols()==true. La requête est abaissée par lower_case ASCII dans Cursiv et toLowerCase Unicode dans Langium avant le même matcher ASCII ; les noms non ASCII peuvent donc diverger dans les recherches workspace.

**Preuves de comparaison.**

- [langium/packages/langium/src/lsp/workspace-symbol-provider.ts:42](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/workspace-symbol-provider.ts#L42) — Hook de résolution optionnel.
- [langium/packages/langium/src/lsp/workspace-symbol-provider.ts:57](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/workspace-symbol-provider.ts#L57) — Normalisation de requête et parcours.
- [langium/packages/langium/src/lsp/workspace-symbol-provider.ts:72](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/workspace-symbol-provider.ts#L72) — Résultat depuis nameSegment.

**Limites.** Lecture statique de l’implémentation, des contrats et des callers ; aucun test ou code projet exécuté.

<a id="comparison-lsp-032"></a>

### LSP-032 — Repliement des objets et commentaires

**Question.** Comparer les bornes et les catégories des régions repliables.

**Cursiv.** Le parcours des objets respecte shouldProcess et l’élagage shouldProcessContent ; la racine n’est pas repliée comme un objet. Les commentaires sont collectés sur l’ensemble du CST, y compris les trivia externes. Le seuil de trois lignes et la conservation visible du délimiteur final suivent le contrat. Les points d’annulation, en revanche, sont attachés à l’émission de ranges et ne bornent pas le parcours d’un document ne produisant aucun pli.

**Preuves Cursiv.**

- [src/lsp/cursiv/ranges/DefaultFoldingRangeProvider.cpp:26](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/ranges/DefaultFoldingRangeProvider.cpp#L26) — Token vérifié avant calcul et à l’émission.
- [src/lsp/cursiv/ranges/DefaultFoldingRangeProvider.cpp:37](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/ranges/DefaultFoldingRangeProvider.cpp#L37) — Parcours des objets.
- [src/lsp/cursiv/ranges/DefaultFoldingRangeProvider.cpp:82](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/ranges/DefaultFoldingRangeProvider.cpp#L82) — Parcours des commentaires et bornes.

**Comparaison.** Le seuil, l’élagage et la règle de dernière ligne correspondent au défaut Langium. Langium sélectionne les commentaires via GrammarConfig.multilineCommentRules ; Cursiv traite les records hidden comme commentaires. Le défaut Langium ignore aussi son token : cette faiblesse d’annulation est commune, pas une régression propre à Cursiv.

**Preuves de comparaison.**

- [langium/packages/langium/src/lsp/folding-range-provider.ts:41](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/folding-range-provider.ts#L41) — Configuration des commentaires et token inutilisé.
- [langium/packages/langium/src/lsp/folding-range-provider.ts:118](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/folding-range-provider.ts#L118) — Bornes des plages.

**Limites.** Lecture statique de l’implémentation, des contrats et des callers ; aucun test ou code projet exécuté.

<a id="comparison-lsp-034"></a>

### LSP-034 — Survol

**Question.** Comparer choix du nœud et personnalisation du contenu au survol.

**Cursiv.** Le hover cherche d’abord les déclarations dénotées puis leurs documentations dans leur langue d’origine ; il rejoint les textes multi-cibles avec un espace. À défaut, une documentation grammaticale du token est recherchée. La plage reste celle du nom source. Le serveur adapte le kind aux préférences client, mais ne transforme pas réellement la syntaxe Markdown lorsque le client préfère du texte brut.

**Preuves Cursiv.**

- [src/lsp/cursiv/hover/DefaultHoverProvider.cpp:42](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/hover/DefaultHoverProvider.cpp#L42) — Priorité déclarations puis mots-clés.
- [src/lsp/cursiv/hover/DefaultHoverProvider.cpp:81](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/hover/DefaultHoverProvider.cpp#L81) — Documents/DocumentationProvider des cibles.
- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:1120](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L1120) — Adaptation du MarkupKind.

**Comparaison.** Langium expose AstNodeHoverProvider.getAstNodeHoverContent/getKeywordHoverContent et joint aussi les documentations multi-cibles. Cursiv passe par DocumentationProvider et permet de remplacer getHoverContent, avec une plage source explicite et un fallback des mots-clés. Le défaut Langium montré produit toujours Markdown et ne réalise pas non plus une conversion en texte brut.

**Preuves de comparaison.**

- [langium/packages/langium/src/lsp/hover-provider.ts:44](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/hover-provider.ts#L44) — Cibles, jointure et Markdown.
- [langium/packages/langium/src/lsp/hover-provider.ts:76](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/hover-provider.ts#L76) — Hooks spécialisés et documentation de mot-clé.

**Limites.** Lecture statique de l’implémentation, des contrats et des callers ; aucun test ou code projet exécuté.

<a id="comparison-lsp-035"></a>

### LSP-035 — Sélection des régions et actions

**Question.** Comparer la désignation des régions auxquelles le langage attache ses règles de formatage.

**Cursiv.** NodeFormatter rattache les actions à des IDs CST et à un côté prepend/append ; les pointeurs de membres sont contraints à la famille du type AST. property(index) serre l’index hors borne sur la dernière occurrence, tandis que keyword(index) choisit une région vide hors borne ; c’est une divergence de commodité avec Langium qui serre aussi l’index des keywords. properties/keywords concatènent les résultats par sélecteur, pas par tri global, comme Langium ; slice a la sémantique de bornes négatives et clamp de Array.slice. interior exige deux régions unitaires et le helper échange les bornes inversées. formatBlock distingue le bloc vide et applique l’indentation aux records intérieurs ; formatSeparatedList attache les deux côtés des séparateurs. fit réunit les mouvements triés mais remet les options à leur défaut, comme Langium. Le défaut concret est l’identification des mots-clés par leur orthographe source, incompatible avec les variantes de casse admises par ikw.

**Preuves Cursiv.**

- [src/lsp/cursiv/formatting/AbstractFormatter.hpp:64](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/formatting/AbstractFormatter.hpp#L64) — Sélection de nœuds et membres.
- [src/lsp/cursiv/formatting/AbstractFormatter.hpp:89](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/formatting/AbstractFormatter.hpp#L89) — Index property clamped.
- [src/lsp/cursiv/formatting/AbstractFormatter.hpp:113](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/formatting/AbstractFormatter.hpp#L113) — Index keyword hors borne vide.
- [src/lsp/cursiv/formatting/FormattingAction.cpp:17](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/formatting/FormattingAction.cpp#L17) — Transmission de côtés au collecteur.
- [src/lsp/cursiv/formatting/FormattingAction.cpp:37](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/formatting/FormattingAction.cpp#L37) — Slice négatif et bornes.
- [src/lsp/cursiv/formatting/FormattingRules.cpp:150](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/formatting/FormattingRules.cpp#L150) — Combinaison fit.
- [src/lsp/cursiv/formatting/FormattingRules.cpp:174](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/formatting/FormattingRules.cpp#L174) — Blocs et listes.
- [src/core/cursiv/syntax-tree/CstUtils.cpp:232](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/syntax-tree/CstUtils.cpp#L232) — Intérieur et inversion des bornes.

**Comparaison.** L’API Cursiv reproduit les régions node/property/keyword/cst/interior et les actions de Langium avec des pointeurs de membres typés et un collector local à la requête. Elle ajoute hidden/hiddens, les callbacks onHidden et les helpers de formes/commentaires. keyword(index) hors borne est vide au lieu de viser la dernière occurrence. La recherche par texte source perd les variantes ikw que Langium retrouve grâce à grammarSource.value. fit remet les options à zéro dans les deux projets : priorité/allowMore doivent être configurés sur l’action résultante si nécessaires.

**Preuves de comparaison.**

- [langium/packages/langium/src/lsp/formatter.ts:596](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/formatter.ts#L596) — Sélecteurs de propriétés et mots-clés.
- [langium/packages/langium/src/lsp/formatter.ts:628](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/formatter.ts#L628) — Intérieur de deux régions.
- [langium/packages/langium/src/lsp/formatter.ts:698](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/formatter.ts#L698) — Slice de régions.
- [langium/packages/langium/src/lsp/formatter.ts:743](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/formatter.ts#L743) — fit ne conserve que les moves.
- [langium/packages/langium/src/utils/grammar-utils.ts:201](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/utils/grammar-utils.ts#L201) — Clamp de keyword(index).
- [langium/packages/langium/src/utils/grammar-utils.ts:213](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/utils/grammar-utils.ts#L213) — Mot-clé de grammaire plutôt que texte source.

**Limites.** Lecture statique uniquement ; aucune exécution du projet ni mesure de coût.

<a id="comparison-lsp-036"></a>

### LSP-036 — Arbitrage des espaces et retraits

**Question.** Comparer priorité et combinaison des contraintes d’espacement.

**Cursiv.** Le collecteur donne priorité au dernier enregistrement de priorité au moins égale pour une même clé. Les mouvements fit sont choisis d’après les retours de ligne déjà présents ; allowMore a priorité sur allowLess si les deux sont vrais. Les additions de niveaux et conversions de comptes sont contrôlées en int64, tabSize est borné à1..64, les comptes négatifs de chaînes sont saturés à0. Le style CRLF/LF/CR vient du premier terminateur du document ; les edits identiques sont comparés aux octets originaux sans suppression des CR. Le parcours CST et ses restaurations de niveaux sont itératifs ; le tri stable final élimine doublons exacts et chevauchements. Cela garantit une liste compatible pour le client, mais l’arbitrage n’est pas global par gap : une priorité forte peut être perdue entre append et prepend.

**Preuves Cursiv.**

- [src/lsp/cursiv/formatting/FormattingEngine.cpp:54](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/formatting/FormattingEngine.cpp#L54) — tabSize borné.
- [src/lsp/cursiv/formatting/FormattingEngine.cpp:59](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/formatting/FormattingEngine.cpp#L59) — Comptes vérifiés.
- [src/lsp/cursiv/formatting/FormattingEngine.cpp:189](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/formatting/FormattingEngine.cpp#L189) — allowMore/allowLess et fit.
- [src/lsp/cursiv/formatting/FormattingEngine.cpp:436](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/formatting/FormattingEngine.cpp#L436) — Émission du gap et contexte.
- [src/lsp/cursiv/formatting/FormattingEngine.cpp:466](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/formatting/FormattingEngine.cpp#L466) — Tri, chevauchements et no-op.
- [src/lsp/cursiv/formatting/FormattingEngine.cpp:526](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/formatting/FormattingEngine.cpp#L526) — Priorité par clé.
- [src/lsp/cursiv/formatting/FormattingEngine.cpp:573](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/formatting/FormattingEngine.cpp#L573) — Pile explicite.
- [src/lsp/cursiv/formatting/LineTerminator.hpp:17](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/formatting/LineTerminator.hpp#L17) — CR, LF et CRLF reconnus.

**Comparaison.** Priorités par node/side, choix fit par nombre de lignes et traitement allowMore/allowLess reprennent la logique Langium. Le contournement de priorité entre deux côtés est donc un comportement partagé, pas une régression propre à Cursiv. Cursiv préserve les terminaisons détectées, trie les edits au besoin et supprime les doublons exacts, alors que Langium émet des LF et retire CR dans son test de nécessité ; le collector Cursiv appartient aussi à la requête plutôt qu’au provider.

**Preuves de comparaison.**

- [langium/packages/langium/src/lsp/formatter.ts:131](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/formatter.ts#L131) — Collecteur par node/side.
- [langium/packages/langium/src/lsp/formatter.ts:150](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/formatter.ts#L150) — Fusion sans priorité.
- [langium/packages/langium/src/lsp/formatter.ts:207](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/formatter.ts#L207) — Test de nécessité supprimant CR.
- [langium/packages/langium/src/lsp/formatter.ts:426](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/formatter.ts#L426) — Production de LF.
- [langium/packages/langium/src/lsp/formatter.ts:450](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/formatter.ts#L450) — allowMore/allowLess et fit.

**Limites.** Lecture statique uniquement ; aucune exécution du projet ni mesure de coût.

<a id="comparison-lsp-037"></a>

### LSP-037 — Nœuds masqués et commentaires

**Question.** Comparer la participation des commentaires et autres tokens masqués.

**Cursiv.** Les tokens hidden sont trouvés par leur règle de terminal et les callbacks par une clé string propriétaire. replace ne stocke rien si le texte ne change pas ; plusieurs remplacements gardent le dernier. Les callbacks peuvent agrandir le pool d’actions : le moteur relit les pointeurs ensuite et le pending append conserve une clé, évitant des pointeurs de vector invalidés. La réécriture du commentaire est séparée des marges et incorpore la marge dans le remplacement si les offsets coïncident ; sans remplacement, chaque ligne reçoit seulement un edit de marge. Les helpers conservent marqueurs, style de ligne, plafond de lignes vides et structure de tags selon les options. Deux limites ressortent : les actions d’espacement prepend ne sont pas exécutées sur un hidden et une première ligne de corps blanche rend le helper multiligne non idempotent.

**Preuves Cursiv.**

- [src/lsp/cursiv/formatting/AbstractFormatter.cpp:37](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/formatting/AbstractFormatter.cpp#L37) — Sélection des hidden par nom de règle.
- [src/lsp/cursiv/formatting/AbstractFormatter.cpp:96](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/formatting/AbstractFormatter.cpp#L96) — Dispatch des callbacks.
- [src/lsp/cursiv/formatting/FormattingAction.cpp:60](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/formatting/FormattingAction.cpp#L60) — Marge contrôlée et remplacement optionnel.
- [src/lsp/cursiv/formatting/FormattingEngine.cpp:343](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/formatting/FormattingEngine.cpp#L343) — Composition marge/remplacement.
- [src/lsp/cursiv/formatting/FormattingEngine.cpp:376](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/formatting/FormattingEngine.cpp#L376) — Édits de marges sans réécriture du contenu.
- [src/lsp/cursiv/formatting/FormattingEngine.cpp:633](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/formatting/FormattingEngine.cpp#L633) — Callback puis relecture des pointeurs.
- [src/lsp/cursiv/formatting/FormattingRules.cpp:205](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/formatting/FormattingRules.cpp#L205) — Helper commentaire de ligne.
- [src/lsp/cursiv/formatting/FormattingRules.cpp:223](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/formatting/FormattingRules.cpp#L223) — Helper multiligne et tags.

**Comparaison.** Langium réindente les hidden mais quitte aussi immédiatement pour un commentaire situé après un token sur la même ligne et ne consomme que l’indentation du mouvement. Cursiv conserve cette restriction tout en ajoutant des callbacks par terminal et une API de remplacement du contenu, absents de ce formatter Langium. Le défaut d’idempotence relève du nouveau helper de contenu Cursiv ; la restriction prepend/hidden est partagée.

**Preuves de comparaison.**

- [langium/packages/langium/src/lsp/formatter.ts:270](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/formatter.ts#L270) — Hidden inline laissé en place.
- [langium/packages/langium/src/lsp/formatter.ts:287](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/formatter.ts#L287) — Seule la marge est calculée.
- [langium/packages/langium/src/lsp/formatter.ts:361](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/formatter.ts#L361) — Seul tabs modifie le compte d’indentation.
- [langium/packages/langium/src/lsp/formatter.ts:369](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/formatter.ts#L369) — Les hidden passent dans la branche spécifique.

**Limites.** Lecture statique uniquement ; aucune exécution du projet ni mesure de coût.

<a id="comparison-lsp-038"></a>

### LSP-038 — Admission document, plage et frappe

**Question.** Comparer préconditions de parsing, respect d’une plage et extension du formatage à la frappe.

**Cursiv.** Le formatage document refuse tout diagnostic syntaxique ; les conversions et diagnostics de budget ne sont pas assimilés à une réparation de syntaxe. Le formatage de plage/frappe n’est admis que si la première erreur syntaxique se situe après la fin de plage. La frappe fabrique une sélection allant du début de la ligne au caret, mais le filtrage par lignes peut produire des edits après le caret ou traversant une borne : ce choix est explicitement documenté. Le serveur route les trois modes à Parsed et vérifie le caractère déclencheur du formatter courant après l’union des capacités de session. Les providers par défaut n’annoncent aucun trigger. Les options reçues sont transmises, avec tabSize borné par le moteur. Le prédicat d’intersection asymétrique peut toutefois ignorer un sous-arbre qui recouvre seulement le début de la plage.

**Preuves Cursiv.**

- [src/lsp/cursiv/formatting/AbstractFormatter.cpp:46](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/formatting/AbstractFormatter.cpp#L46) — Admission document.
- [src/lsp/cursiv/formatting/AbstractFormatter.cpp:55](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/formatting/AbstractFormatter.cpp#L55) — Admission range et frappe.
- [src/lsp/cursiv/formatting/AbstractFormatter.cpp:78](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/formatting/AbstractFormatter.cpp#L78) — Première erreur syntaxique.
- [src/core/cursiv/workspace/Document.cpp:40](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/Document.cpp#L40) — parseRecovered fondé sur isSyntax.
- [src/core/cursiv/syntax-tree/ParseDiagnostic.hpp:56](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/syntax-tree/ParseDiagnostic.hpp#L56) — Conversion/budget exclus de isSyntax.
- [src/lsp/cursiv/formatting/Formatter.hpp:29](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/formatting/Formatter.hpp#L29) — Extension par lignes documentée.
- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:1201](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L1201) — Routage Parsed.
- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:1234](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L1234) — Trigger propre à la langue.

**Comparaison.** Document/refus des erreurs, formatage avant la première erreur et frappe sur la ligne courante suivent Langium. Les deux implémentations filtrent par lignes et partagent le même insideRange asymétrique. Cursiv rend cette extension explicite dans son interface et filtre centralement les triggers de chaque langue ; aucun mode de formatage strictement contenu dans les colonnes sélectionnées n’est fourni par ce provider.

**Preuves de comparaison.**

- [langium/packages/langium/src/lsp/formatter.ts:68](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/formatter.ts#L68) — Refus de formatage document sur erreur.
- [langium/packages/langium/src/lsp/formatter.ts:86](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/formatter.ts#L86) — Admission avant première erreur.
- [langium/packages/langium/src/lsp/formatter.ts:110](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/formatter.ts#L110) — Frappe construite comme plage de ligne.
- [langium/packages/langium/src/lsp/formatter.ts:195](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/formatter.ts#L195) — Filtrage par lignes partagé.

**Limites.** Lecture statique uniquement ; aucune exécution du projet ni mesure de coût.

<a id="comparison-lsp-039"></a>

### LSP-039 — Classification et légende

**Question.** Comparer la personnalisation de la légende et la correspondance entre indices émis et types annoncés.

**Cursiv.** Les8valeurs enum correspondent exactement aux8entrées de légende et le cinquième entier est toujours0, cohérent avec l’absence de modifiers. La classification générique donne Type aux déclarations possédant d’autres déclarations, Property aux autres noms, Variable aux références résolues ou non, Comment à tout hidden, puis string/number selon le premier caractère et keyword/operator selon le littéral. Ce sont des heuristiques documentées, pas une résolution sémantique de fonctions/paramètres/types. Les spans sont triés par début, largeur décroissante et priorité aux noms, puis les chevauchements sont abandonnés ; ce choix est déterministe. La spécialisation de légende reste impossible via le slot standard et la classe générique n’expose pas de hook de classification par nœud.

**Preuves Cursiv.**

- [src/lsp/cursiv/semantic/SemanticTokenProvider.cpp:12](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/semantic/SemanticTokenProvider.cpp#L12) — Ordre de légende.
- [src/lsp/cursiv/semantic/SemanticTokenProvider.hpp:18](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/semantic/SemanticTokenProvider.hpp#L18) — Correspondance enum.
- [src/lsp/cursiv/semantic/GrammarSemanticTokenProvider.cpp:135](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/semantic/GrammarSemanticTokenProvider.cpp#L135) — Déclarations selon ownership.
- [src/lsp/cursiv/semantic/GrammarSemanticTokenProvider.cpp:154](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/semantic/GrammarSemanticTokenProvider.cpp#L154) — Références y compris non résolues.
- [src/lsp/cursiv/semantic/GrammarSemanticTokenProvider.cpp:171](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/semantic/GrammarSemanticTokenProvider.cpp#L171) — Classification des feuilles.
- [src/lsp/cursiv/semantic/GrammarSemanticTokenProvider.cpp:216](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/semantic/GrammarSemanticTokenProvider.cpp#L216) — Tri stable et arbitrage.
- [src/lsp/cursiv/semantic/GrammarSemanticTokenProvider.hpp:31](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/semantic/GrammarSemanticTokenProvider.hpp#L31) — Pipeline highlight privé.

**Comparaison.** Langium fournit23types standard et10modifiers, des getters redéfinissables et un hook highlightElement/acceptor pour définir les catégories du langage ; les options de providers sont fusionnées avec contrôle des indices. Cursiv offre une coloration automatique immédiatement disponible à partir de la grammaire, mais seulement8types globaux et aucun modifier. Cet écart est gênant pour un langage dont l’éditeur doit distinguer fonctions, paramètres, namespaces ou symboles readonly.

**Preuves de comparaison.**

- [langium/packages/langium/src/lsp/semantic-token-provider.ts:21](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/semantic-token-provider.ts#L21) — 23types standard.
- [langium/packages/langium/src/lsp/semantic-token-provider.ts:47](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/semantic-token-provider.ts#L47) — 10modifiers.
- [langium/packages/langium/src/lsp/semantic-token-provider.ts:261](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/semantic-token-provider.ts#L261) — Getters de catégories et options.
- [langium/packages/langium/src/lsp/semantic-token-provider.ts:375](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/semantic-token-provider.ts#L375) — Hook de classification du langage.
- [langium/packages/langium/src/lsp/semantic-token-provider.ts:383](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/semantic-token-provider.ts#L383) — Encodage des types et modifiers choisis.

**Limites.** Lecture statique uniquement ; aucune exécution du projet ni mesure de coût.

<a id="comparison-lsp-040"></a>

### LSP-040 — Encodage et découpage des spans

**Question.** Comparer couverture des modes full/range/delta et leur incidence sur le contrat de sortie.

**Cursiv.** encode émet exactement5entiers, avec delta de ligne puis colonne absolue après changement de ligne ou relative sur la même ligne ; les longueurs nulles sont ignorées. Le tri et la borne emitted imposent l’ordre sans chevauchement avant le Cursor. Celui-ci convertit progressivement les offsets source en UTF-16, saute les fins de ligne et traite CRLF comme une terminaison ; emit découpe chaque span multiligne avant ces terminateurs. Range retient tout token qui touche la fenêtre, sans couper un token à la borne, conformément à son contrat ; les feuilles utilisent un index de fins mais noms/références sont toujours collectés sur le document. Les deux routes attendent Linked. Aucun défaut d’ordre, de longueur ou d’indice de légende n’est démontré sur ces chemins ; la lacune concerne l’extension delta, et non une option active par défaut dans Langium.

**Preuves Cursiv.**

- [src/lsp/cursiv/semantic/GrammarSemanticTokenProvider.cpp:45](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/semantic/GrammarSemanticTokenProvider.cpp#L45) — Encodage des cinq entiers.
- [src/lsp/cursiv/semantic/GrammarSemanticTokenProvider.cpp:64](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/semantic/GrammarSemanticTokenProvider.cpp#L64) — Découpage par ligne.
- [src/lsp/cursiv/semantic/GrammarSemanticTokenProvider.cpp:109](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/semantic/GrammarSemanticTokenProvider.cpp#L109) — Offsets de la plage.
- [src/lsp/cursiv/semantic/GrammarSemanticTokenProvider.cpp:131](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/semantic/GrammarSemanticTokenProvider.cpp#L131) — Toucher la plage suffit ; token entier.
- [src/lsp/cursiv/semantic/GrammarSemanticTokenProvider.cpp:205](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/semantic/GrammarSemanticTokenProvider.cpp#L205) — Index de feuilles.
- [src/lsp/cursiv/semantic/GrammarSemanticTokenProvider.cpp:229](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/semantic/GrammarSemanticTokenProvider.cpp#L229) — Exclusion des chevauchements.
- [src/core/cursiv/workspace/TextDocument.cpp:337](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/TextDocument.cpp#L337) — Cursor monotone et conversion.
- [src/core/cursiv/workspace/TextDocument.cpp:352](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/TextDocument.cpp#L352) — Franchissement de lignes et UTF-16.
- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:1050](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L1050) — Routes full/range àLinked.

**Comparaison.** Full et range sont les modes annoncés par défaut dans les deux frameworks : Langium désactive aussi delta par défaut. La différence est son point d’extension delta déjà présent et configurable. Langium peut émettre un token multiligne si le client le supporte ; Cursiv choisit systématiquement le découpage, qui reste valide pour ces clients aussi. Langium expose un parcours AST filtré par range et un builder ; Cursiv construit des spans absolus puis trie/encode en UTF-16, avec des scans globaux des noms/références même pour une petite plage.

**Preuves de comparaison.**

- [langium/packages/langium/src/lsp/semantic-token-provider.ts:269](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/semantic-token-provider.ts#L269) — Options par défaut full/range et delta=false.
- [langium/packages/langium/src/lsp/semantic-token-provider.ts:285](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/semantic-token-provider.ts#L285) — Modes full/range/delta.
- [langium/packages/langium/src/lsp/semantic-token-provider.ts:356](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/semantic-token-provider.ts#L356) — Parcours AST filtré par range.
- [langium/packages/langium/src/lsp/semantic-token-provider.ts:396](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/semantic-token-provider.ts#L396) — Multiligne selon capacité cliente ou découpage.

**Limites.** Lecture statique uniquement ; aucune exécution du projet ni mesure de coût.

<a id="comparison-lsp-041"></a>

### LSP-041 — Actions issues des diagnostics et versions

**Question.** Comparer création, résolution et provenance des edits des actions de code.

**Cursiv.** Les quick-fixes sont construits depuis les diagnostics actuels dans la plage demandée, pas depuis des offsets client périmés. Chaque payload est contrôlé sur type, révision et bornes ; le diagnostic client n’est repris que s’il correspond au diagnostic courant. Les actions personnalisées sont ajoutées après le filtre de quick-fix. Le coût de spelling_actions est suivi en VAL-005. Le versionnement global de toutes les actions peut toutefois écraser un WorkspaceEdit déjà complet.

**Preuves Cursiv.**

- [src/lsp/cursiv/actions/DefaultCodeActionProvider.cpp:98](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/actions/DefaultCodeActionProvider.cpp#L98) — Validation des données de fix.
- [src/lsp/cursiv/actions/DefaultCodeActionProvider.cpp:159](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/actions/DefaultCodeActionProvider.cpp#L159) — Sélection par plage et extension.
- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:1162](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L1162) — Versionnement après provider.
- [src/lsp/cursiv/runtime/LanguageServerRuntime.cpp:602](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/LanguageServerRuntime.cpp#L602) — Adaptation aux clients sans literals.

**Comparaison.** Langium expose une interface générique retournant Command ou CodeAction, sans un défaut générique de quick-fix dans ce fichier. Cursiv fournit un protocole de fixes intégré à Diagnostic.data, un hook d’ajout et un versionnement automatique. Ni l’interface Langium citée ni Cursiv ne propose ici codeAction/resolve. Cursiv n’expose pas Command directement, mais un CodeAction ne portant que command est adapté pour les anciens clients.

**Preuves de comparaison.**

- [langium/packages/langium/src/lsp/code-action.ts:12](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/code-action.ts#L12) — Union Command/CodeAction.
- [langium/packages/langium/src/lsp/language-server.ts:415](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/language-server.ts#L415) — Délégation au provider.

**Limites.** Audit statique uniquement ; la logique métier des providers optionnels dépend du langage qui les installe.

<a id="comparison-lsp-042"></a>

### LSP-042 — Provider de déclaration

**Question.** Comparer ce qui est fourni par défaut et les hooks requis pour le langage.

**Cursiv.** DeclarationProvider est un slot distinct, optionnel, annoncé seulement s’il est installé. Le routage attend Linked et transmet document/position/token ; la variante de LocationLink est adaptée aux capacités client. La nullabilité annoncée par l’interface du provider est cependant effacée par le wrapper serveur.

**Preuves Cursiv.**

- [src/lsp/cursiv/navigation/DeclarationProvider.hpp:20](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/navigation/DeclarationProvider.hpp#L20) — Contrat optional et vide.
- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:301](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L301) — Capacité conditionnelle.
- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:952](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L952) — Wrapper de déclaration.

**Comparaison.** Langium fournit aussi uniquement une interface de déclaration à implémenter par langue. Il reçoit DeclarationParams complets et conserve undefined jusqu’au handler, qui le convertit en null. Cursiv transmet seulement la position et perd la distinction optionnelle avant le transport.

**Preuves de comparaison.**

- [langium/packages/langium/src/lsp/declaration-provider.ts:15](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/declaration-provider.ts#L15) — Interface Langium.
- [langium/packages/langium/src/lsp/language-server.ts:806](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/language-server.ts#L806) — Normalisation finale de undefined.

**Limites.** Audit statique uniquement ; la logique métier des providers optionnels dépend du langage qui les installe.

<a id="comparison-lsp-043"></a>

### LSP-043 — Provider de définition de type

**Question.** Comparer ce qui est fourni par défaut et les hooks requis pour le langage.

**Cursiv.** Le slot de définition de type est annoncé séparément et attend Linked. Il ne contient aucune logique de types implicite ; une langue doit implémenter getTypeDefinition et peut employer collect_links_per_declaration pour résoudre les cibles du caret. La question de nullopt est partagée avec LSP-042. Aucun autre défaut de routage constaté.

**Preuves Cursiv.**

- [src/lsp/cursiv/navigation/TypeDefinitionProvider.hpp:16](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/navigation/TypeDefinitionProvider.hpp#L16) — Interface de type.
- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:966](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L966) — Phase et délégation.
- [src/lsp/cursiv/navigation/NavigationProviderSupport.cpp:22](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/navigation/NavigationProviderSupport.cpp#L22) — Helper retournant les liens collectés.

**Comparaison.** Langium ajoute AbstractTypeDefinitionProvider et un hook collectGoToTypeLocationLinks, quand Cursiv propose un helper libre à composer. Réserve sur l’implémentation Langium 4.4.0 auditée : elle accumule links dans getTypeDefinition sans les retourner et finit toujours par undefined. La présence de cette classe ne prouve donc pas un meilleur comportement fourni que Cursiv à cette révision.

**Preuves de comparaison.**

- [langium/packages/langium/src/lsp/type-provider.ts:26](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/type-provider.ts#L26) — Classe abstraite.
- [langium/packages/langium/src/lsp/type-provider.ts:34](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/type-provider.ts#L34) — Collecte puis retour undefined.
- [langium/packages/langium/src/lsp/type-provider.ts:55](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/type-provider.ts#L55) — Hook métier.

**Limites.** Audit statique uniquement ; la logique métier des providers optionnels dépend du langage qui les installe.

<a id="comparison-lsp-044"></a>

### LSP-044 — Provider d’implémentations

**Question.** Comparer ce qui est fourni par défaut et les hooks requis pour le langage.

**Cursiv.** La recherche d’implémentations utilise un slot distinct de définition/type et attend WorkspaceState::IndexedReferences. Cela permet à une langue de parcourir l’index inverse sous une lecture normale après la phase globale. La logique de ce qu’est une implémentation reste explicitement absente du framework. Nullopt est effacé comme relevé en LSP-042.

**Preuves Cursiv.**

- [src/lsp/cursiv/navigation/ImplementationProvider.hpp:16](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/navigation/ImplementationProvider.hpp#L16) — Interface indépendante.
- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:982](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L982) — Attente de l’index workspace.
- [src/lsp/cursiv/navigation/NavigationProviderSupport.cpp:22](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/navigation/NavigationProviderSupport.cpp#L22) — Collecte partageable par cible.

**Comparaison.** Langium fournit AbstractGoToImplementationProvider et un hook de collecte ; Cursiv fournit l’interface et le helper libre. Le défaut Langium 4.4.0 lu présente le même problème que celui du type : links est rempli sans être renvoyé. Les deux routeurs demandent l’index workspace pour cette méthode.

**Preuves de comparaison.**

- [langium/packages/langium/src/lsp/implementation-provider.ts:27](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/implementation-provider.ts#L27) — Classe et hook.
- [langium/packages/langium/src/lsp/implementation-provider.ts:36](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/implementation-provider.ts#L36) — Collecte non retournée.
- [langium/packages/langium/src/lsp/language-server.ts:446](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/language-server.ts#L446) — Pré-requis workspace.

**Limites.** Audit statique uniquement ; la logique métier des providers optionnels dépend du langage qui les installe.

<a id="comparison-lsp-045"></a>

### LSP-045 — Aide à la signature

**Question.** Comparer déclencheurs, contexte et sélection de signature exposés aux extensions.

**Cursiv.** SignatureHelp conserve les params complets, notamment context/retrigger et activeSignatureHelp. Les listes de trigger/retrigger sont fusionnées et dédupliquées entre langues, tandis que l’existence du provider suffit à annoncer la capacité, même sans caractère automatique. Le wrapper attend IndexedReferences puis transmet le token. Aucun motif de requête perdu dans ce provider.

**Preuves Cursiv.**

- [src/lsp/cursiv/semantic/SignatureHelpProvider.hpp:27](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/semantic/SignatureHelpProvider.hpp#L27) — Contexte explicitement transmis.
- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:311](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L311) — Fusion des triggers.
- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:395](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L395) — Annonce avec ou sans triggers.
- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:1144](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L1144) — Délégation complète.

**Comparaison.** Langium expose les mêmes paramètres et offre en plus AbstractSignatureHelpProvider qui trouve un AST puis appelle getSignatureFromElement, avec '(' et ',' par défaut. Cursiv exige l’implémentation de cette sélection. La fusion Langium ne renvoie une option que s’il existe des triggerCharacters ; Cursiv garde la capacité lorsque le service existe et peut donc servir une invocation explicite sans trigger.

**Preuves de comparaison.**

- [langium/packages/langium/src/lsp/signature-help-provider.ts:17](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/signature-help-provider.ts#L17) — Interface complète.
- [langium/packages/langium/src/lsp/signature-help-provider.ts:28](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/signature-help-provider.ts#L28) — Classe d’aide et triggers.
- [langium/packages/langium/src/lsp/signature-help-provider.ts:60](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/signature-help-provider.ts#L60) — Fusion et condition finale.

**Limites.** Audit statique uniquement ; la logique métier des providers optionnels dépend du langage qui les installe.

<a id="comparison-lsp-046"></a>

### LSP-046 — Indications intégrées

**Question.** Comparer calcul et résolution différée des indications intégrées.

**Cursiv.** InlayHintProvider reçoit la plage demandée et le token. Le serveur attend la phase IndexedReferences du document ; aucune indication n’est calculée par défaut. initialize annonce resolveProvider=false, cohérent avec l’absence de hook de résolution. La langue doit retourner des hints déjà utilisables et respecter la plage.

**Preuves Cursiv.**

- [src/lsp/cursiv/semantic/InlayHintProvider.hpp:19](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/semantic/InlayHintProvider.hpp#L19) — Contrat borné par range.
- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:325](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L325) — Resolve désactivé.
- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:1075](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L1075) — Routage de plage et token.

**Comparaison.** Langium possède le même service optionnel sans resolve dans l’interface et annonce également false. Il apporte AbstractInlayHintProvider qui parcourt streamAst avec la plage et sonde l’annulation ; Cursiv ne possède pas cet adaptateur de visite dans la couche LSP et le langage doit effectuer son parcours.

**Preuves de comparaison.**

- [langium/packages/langium/src/lsp/inlay-hint-provider.ts:20](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/inlay-hint-provider.ts#L20) — Interface sans resolve.
- [langium/packages/langium/src/lsp/inlay-hint-provider.ts:30](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/inlay-hint-provider.ts#L30) — Visiteur abstrait sur range.
- [langium/packages/langium/src/lsp/language-server.ts:187](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/language-server.ts#L187) — Resolve false.

**Limites.** Audit statique uniquement ; la logique métier des providers optionnels dépend du langage qui les installe.

<a id="comparison-lsp-047"></a>

### LSP-047 — Liens du document

**Question.** Comparer les responsabilités de calcul et de résolution des liens.

**Cursiv.** DocumentLinkProvider est optionnel ; la capacité et le routage sont cohérents. Le document arrive après Parsed sous read lock et le token est transmis. Sans étape de resolve, les cibles des liens doivent être déterminées pendant cet appel. Le provider peut renvoyer la liste vide sans erreur.

**Preuves Cursiv.**

- [src/lsp/cursiv/actions/DocumentLinkProvider.hpp:16](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/actions/DocumentLinkProvider.hpp#L16) — Interface de calcul.
- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:330](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L330) — Capacité sans resolve.
- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:1173](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L1173) — Routage après parse.

**Comparaison.** L’interface Langium document-link-provider expose aussi seulement getDocumentLinks et son serveur annonce resolveProvider=false. L’écart est principalement le retrait des DocumentLinkParams complets dans Cursiv ; aucune fonctionnalité de résolution différée présente dans Langium n’est manquante ici.

**Preuves de comparaison.**

- [langium/packages/langium/src/lsp/document-link-provider.ts:15](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/document-link-provider.ts#L15) — Interface Langium.
- [langium/packages/langium/src/lsp/language-server.ts:180](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/language-server.ts#L180) — Resolve désactivé.

**Limites.** Audit statique uniquement ; la logique métier des providers optionnels dépend du langage qui les installe.

<a id="comparison-lsp-048"></a>

### LSP-048 — Code lenses

**Question.** Comparer les responsabilités de calcul et de résolution des lenses.

**Cursiv.** CodeLensProvider exige une réponse complète et annonce explicitement resolveProvider=false. Le token est transmis et les commandes restent sous responsabilité du provider. La phase est IndexedReferences du document, comme Langium ; un provider qui compte des références globales doit adapter le prérequis du serveur, limite d’extension suivie en LSP-019, car une phase documentaire seule n’exprime pas la complétude de l’index workspace.

**Preuves Cursiv.**

- [src/lsp/cursiv/actions/CodeLensProvider.hpp:10](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/actions/CodeLensProvider.hpp#L10) — Résultat complet et absence de resolve.
- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:335](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L335) — Capacité conditionnelle.
- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:1188](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L1188) — Pré-requis documentaire.

**Comparaison.** Langium expose également provideCodeLens sans resolve et annonce false. Son handler demande aussi DocumentState.IndexedReferences par défaut, mais accepte requiredState en argument, ce qui permet d’imposer WorkspaceState.IndexedReferences pour un compteur global ; cette adaptation est moins directe dans Cursiv.

**Preuves de comparaison.**

- [langium/packages/langium/src/lsp/code-lens-provider.ts:12](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/code-lens-provider.ts#L12) — Interface de lenses.
- [langium/packages/langium/src/lsp/language-server.ts:183](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/language-server.ts#L183) — Annonce sans resolve.
- [langium/packages/langium/src/lsp/language-server.ts:595](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/language-server.ts#L595) — Pré-requis paramétrable.

**Limites.** Audit statique uniquement ; la logique métier des providers optionnels dépend du langage qui les installe.

<a id="comparison-lsp-049"></a>

### LSP-049 — Préparation et parcours des appels

**Question.** Comparer conservation d’identité entre préparation et parcours des appels.

**Cursiv.** La préparation peut renvoyer une entrée par cible ; les expansions routent selon l’URI de l’item après l’index workspace. Le helper recherche d’abord un nom commençant à selectionRange.start, puis la cible locale la plus étroite, ce qui traite les ensembles surchargés sans garder de pointeur entre requêtes. Cette absence de pointeur n’assure pas l’identité logique d’un item après édition.

**Preuves Cursiv.**

- [src/lsp/cursiv/hierarchy/CallHierarchyProvider.hpp:20](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/hierarchy/CallHierarchyProvider.hpp#L20) — Contrats de préparation/expansion.
- [src/lsp/cursiv/hierarchy/HierarchyProviderSupport.cpp:22](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/hierarchy/HierarchyProviderSupport.cpp#L22) — Cibles multiples et reconstruction.
- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:1243](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L1243) — Routage préparation et expansions.

**Comparaison.** Langium fournit une classe abstraite qui crée les items et reconstitue ensuite un nœud depuis item.uri et item.range.start, sans identifiant durable par défaut. Cursiv laisse la création des items à la langue et emploie selectionRange pour mieux viser les noms. La sensibilité des items conservés après modifications existe dans les deux modèles ; les champs data sont disponibles pour une stratégie propre au langage.

**Preuves de comparaison.**

- [langium/packages/langium/src/lsp/call-hierarchy-provider.ts:69](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/call-hierarchy-provider.ts#L69) — Création d’item par défaut.
- [langium/packages/langium/src/lsp/call-hierarchy-provider.ts:90](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/call-hierarchy-provider.ts#L90) — Reconstruction par position.
- [langium/packages/langium/src/lsp/call-hierarchy-provider.ts:114](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/call-hierarchy-provider.ts#L114) — Hooks métier.

**Limites.** Audit statique uniquement ; la logique métier des providers optionnels dépend du langage qui les installe.

<a id="comparison-lsp-050"></a>

### LSP-050 — Préparation et parcours des types

**Question.** Comparer conservation d’identité et hooks de parcours des super/sous-types.

**Cursiv.** La hiérarchie de types utilise les mêmes primitives de ciblage que celle d’appels et la même phase workspace IndexedReferences. supertypes/subtypes reçoivent l’item par valeur logique, sans accès direct à un ancien AST. Le helper n’est pas un registre d’identités persistantes ; la réserve après édition est la même que pour les appels.

**Preuves Cursiv.**

- [src/lsp/cursiv/hierarchy/TypeHierarchyProvider.hpp:20](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/hierarchy/TypeHierarchyProvider.hpp#L20) — Trois méthodes indépendantes.
- [src/lsp/cursiv/hierarchy/HierarchyProviderSupport.cpp:32](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/hierarchy/HierarchyProviderSupport.cpp#L32) — Reconstruction commune.
- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:1282](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L1282) — Routage des relations de types.

**Comparaison.** Langium fournit AbstractTypeHierarchyProvider pour l’enveloppe et les hooks getSupertypes/getSubtypes ; Cursiv fournit les interfaces et le helper partagé. Les deux reconstructions par position exigent une stratégie supplémentaire si les items doivent rester valides après déplacement d’une déclaration ; aucun système persistant d’identité n’est fourni par défaut.

**Preuves de comparaison.**

- [langium/packages/langium/src/lsp/type-hierarchy-provider.ts:69](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/type-hierarchy-provider.ts#L69) — Enveloppe d’item.
- [langium/packages/langium/src/lsp/type-hierarchy-provider.ts:102](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/type-hierarchy-provider.ts#L102) — Reconstruction des supertypes.
- [langium/packages/langium/src/lsp/type-hierarchy-provider.ts:121](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/type-hierarchy-provider.ts#L121) — Reconstruction des subtypes.

**Limites.** Audit statique uniquement ; la logique métier des providers optionnels dépend du langage qui les installe.

<a id="comparison-lsp-051"></a>

### LSP-051 — Registre et exécution des commandes

**Question.** Comparer enregistrement, arguments et erreurs des commandes personnalisées.

**Cursiv.** La table de commandes est construite sous call_once à la première lecture ; une exception de construction permet une nouvelle tentative et emplace réinitialise la table partielle. Le dernier enregistrement d’un nom remplace son handler sans changer l’ordre annoncé. L’exécution passe les arguments et le token et retourne nullopt pour un nom inconnu, conformément au contrat. Le serveur ne verrouille pas l’exécution : la commande choisit ses propres lectures/écritures.

**Preuves Cursiv.**

- [src/lsp/cursiv/services/AbstractExecuteCommandHandler.cpp:26](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/services/AbstractExecuteCommandHandler.cpp#L26) — Construction paresseuse et remplacement.
- [src/lsp/cursiv/services/AbstractExecuteCommandHandler.cpp:51](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/services/AbstractExecuteCommandHandler.cpp#L51) — Lookup et délégation.
- [src/lsp/cursiv/services/ExecuteCommandHandler.hpp:36](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/services/ExecuteCommandHandler.hpp#L36) — Contrat du nom inconnu.
- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:1354](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L1354) — Commandes sans verrou imposé.

**Comparaison.** Langium utilise une Map remplie dans le constructeur et accepte MaybePromise ; le dernier handler d’un nom gagne aussi, et un nom inconnu renvoie undefined. Cursiv diffère par l’initialisation paresseuse thread-safe et la signature synchrone. Les arguments restent une liste JSON et le token est transmis dans les deux API.

**Preuves de comparaison.**

- [langium/packages/langium/src/lsp/execute-command-handler.ts:21](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/execute-command-handler.ts#L21) — Table Map et construction.
- [langium/packages/langium/src/lsp/execute-command-handler.ts:33](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/execute-command-handler.ts#L33) — Résultat et arguments.

**Limites.** Audit statique uniquement ; la logique métier des providers optionnels dépend du langage qui les installe.

<a id="comparison-lsp-052"></a>

### LSP-052 — Lecture de documents par schéma

**Question.** Comparer lecture et rafraîchissement des documents virtuels par schéma.

**Cursiv.** Le provider de contenu copie le texte détenu après vérification du schéma, comparé sans casse ASCII. Le serveur attend impérativement le scan et prend le read lock ; URI inconnue ou schéma non listé retourne null. Aucun accès à un document hors de la liste promise n’est fourni par défaut. Le hook lui-même est synchrone sans token ; un provider réalisant de l’I/O doit donc définir sa propre stratégie d’annulation.

**Preuves Cursiv.**

- [src/lsp/cursiv/content/DefaultTextDocumentContentProvider.cpp:28](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/content/DefaultTextDocumentContentProvider.cpp#L28) — Filtre de schéma et texte courant.
- [src/lsp/cursiv/content/TextDocumentContentProvider.hpp:24](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/content/TextDocumentContentProvider.hpp#L24) — Contrat de lecture.
- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:530](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L530) — Attente scan et lock.

**Comparaison.** Langium attend aussi WorkspaceManager.ready et lit seulement un document enregistré d’un schéma accepté. Son interface est asynchrone et reçoit CancellationToken, différence utile pour un provider spécialisé. Aucun des deux providers comparés ne fournit une méthode de rafraîchissement ; celle-ci doit être émise par la connexion/le client sortant de l’application.

**Preuves de comparaison.**

- [langium/packages/langium/src/lsp/text-document-content-provider.ts:20](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/text-document-content-provider.ts#L20) — API asynchrone avec token.
- [langium/packages/langium/src/lsp/text-document-content-provider.ts:52](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/text-document-content-provider.ts#L52) — Attente et lecture par défaut.

**Limites.** Audit statique uniquement ; la logique métier des providers optionnels dépend du langage qui les installe.

<a id="comparison-lsp-053"></a>

### LSP-053 — Modes de synchronisation et contrat de sauvegarde

**Question.** Comparer l’annonce et l’extension des événements didSave, willSave et willSaveWaitUntil sans confondre simple synchronisation et action avant sauvegarde.

**Cursiv.** La capacité annonce openClose et Incremental, correspondant aux trois handlers didOpen/didChange/didClose effectivement enregistrés. Les options save/willSave/willSaveWaitUntil restent absentes. Ce contrat est cohérent pour la synchronisation courante, mais la surface LanguageServer ne fournit aucun hook typé équivalent aux événements de sauvegarde Langium.

**Preuves Cursiv.**

- [src/lsp/cursiv/runtime/DefaultLanguageServer.cpp:270](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/DefaultLanguageServer.cpp#L270) — Capacités de synchronisation.
- [src/lsp/cursiv/runtime/LanguageServerRuntime.cpp:422](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/LanguageServerRuntime.cpp#L422) — Handlers de document.
- [src/lsp/cursiv/runtime/LanguageServer.hpp:54](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/runtime/LanguageServer.hpp#L54) — Surface de synchronisation publique.

**Comparaison.** Langium expose didSaveDocument, willSaveDocument et willSaveDocumentWaitUntil sur DocumentUpdateHandler, puis négocie et enregistre chacun selon sa présence. Ils ne sont pas tous implémentés par défaut : l’écart est la possibilité d’extension standard fournie à une langue, pas une action automatique avant sauvegarde universellement réalisée par Langium.

**Preuves de comparaison.**

- [langium/packages/langium/src/lsp/document-update-handler.ts:39](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/document-update-handler.ts#L39) — Hooks optionnels de sauvegarde.
- [langium/packages/langium/src/lsp/language-server.ts:147](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/language-server.ts#L147) — Options dérivées de ces hooks.
- [langium/packages/langium/src/lsp/language-server.ts:337](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/language-server.ts#L337) — Raccordement à TextDocuments.

**Limites.** Audit statique uniquement ; la logique métier des providers optionnels dépend du langage qui les installe.

<a id="comparison-pub-003"></a>

### PUB-003 — Association des commentaires à un nœud

**Question.** Comparer l’association du commentaire, les trivia et le choix de la déclaration documentée.

**Cursiv.** find_comment_node rejette les AST sans CST ou Grammar et les racines vides. Une recherche dichotomique sur les fins monotones localise la zone avant le début du nœud ; le parcours inverse ignore les records vides et s’arrête sur un record visible. La racine peut traverser plusieurs trivia initiaux ; un nœud ordinaire s’arrête après le premier record hidden non pertinent. Il n’y a pas de règle supplémentaire d’éloignement en lignes vides. getComment retourne une copie autonome du texte.

**Preuves Cursiv.**

- [src/core/cursiv/documentation/DefaultCommentProvider.cpp:21](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/documentation/DefaultCommentProvider.cpp#L21) — Sélection et frontières.
- [src/core/cursiv/documentation/DefaultCommentProvider.cpp:78](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/documentation/DefaultCommentProvider.cpp#L78) — Copie du commentaire sélectionné.

**Comparaison.** Langium recherche lui aussi le commentaire via le CST et les règles multiline ; il accepte en plus $comment sur un AST désérialisé. Cursiv exige un document/CST pour son provider par défaut, donc un AST sans texte doit fournir un CommentProvider spécifique.

**Preuves de comparaison.**

- [langium/packages/langium/src/documentation/comment-provider.ts:30](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/documentation/comment-provider.ts#L30) — Branche $comment puis recherche CST.
- [langium/packages/langium/src/utils/cst-utils.ts:180](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/utils/cst-utils.ts#L180) — Choix du commentaire précédent.

**Limites.** La politique des trivia dépend de la grammaire et de ses règles de commentaires ; elle n’attache pas automatiquement les commentaires de fin de ligne.

<a id="comparison-pub-004"></a>

### PUB-004 — Analyse des commentaires documentaires

**Question.** Comparer les tags et blocs reconnus ainsi que leur représentation des positions.

**Cursiv.** La tokenisation découpe CR, LF et CRLF, retire uniquement les marqueurs configurés en préfixe/suffixe et garde les coordonnées du texte original. Un tag de bloc peut commencer son contenu par un tag inline ; un inline ne possède qu’un fragment de ligne. boundary évite de rescanner le même suffixe sur plusieurs ouvertures incomplètes. Chaque branche du parseur avance son index ; les plages parentes couvrent le dernier enfant, y compris une ligne vide. Le placement des offsets est un parcours explicite après parsing.

**Preuves Cursiv.**

- [src/core/cursiv/documentation/DocComment.cpp:154](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/documentation/DocComment.cpp#L154) — Recherche des tags inline et borne mémorisée.
- [src/core/cursiv/documentation/DocComment.cpp:259](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/documentation/DocComment.cpp#L259) — Marqueurs et lignes.
- [src/core/cursiv/documentation/DocComment.cpp:376](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/documentation/DocComment.cpp#L376) — Contenu des tags.
- [src/core/cursiv/documentation/DocComment.cpp:681](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/documentation/DocComment.cpp#L681) — Reconstruction des offsets documentaires.

**Comparaison.** Les modèles paragraphes/tags/lignes sont proches. Langium accepte des marqueurs string ou RegExp, fournit des Range en coordonnées JavaScript/UTF-16 et transmet un objet JSDocTag au renderer. Cursiv accepte des marqueurs littéraux et annonce des colonnes en octets, avec un offset supplémentaire : ces ranges ne doivent pas être envoyés directement au LSP.

**Preuves de comparaison.**

- [langium/packages/langium/src/documentation/jsdoc.ts:54](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/documentation/jsdoc.ts#L54) — Options et modèle JSDoc.
- [langium/packages/langium/src/documentation/jsdoc.ts:477](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/documentation/jsdoc.ts#L477) — Normalisation string/RegExp des marqueurs.

**Limites.** Les positions internes comptent des octets et les marqueurs sont littéraux. Pas de parsing récursif de tags inline imbriqués ni de validation d’un langage JSDoc complet.

<a id="comparison-pub-011"></a>

### PUB-011 — Rendu Markdown des commentaires documentaires

**Question.** Comparer les possibilités de rendu personnalisé et les replis des tags inconnus.

**Cursiv.** Le renderer compose les enfants dans l’ordre et réintroduit un saut de ligne quand leurs lignes source diffèrent. renderTag reçoit le contenu déjà rendu et peut produire une chaîne vide sans déclencher le repli. À défaut, `link`/`linkcode`/`linkplain` résolvent cible et libellé, puis appellent renderLink ; les tags inconnus gardent leur nom et contenu. Les paragraphes sont séparés par une ligne vide sauf si la sortie en contient déjà une.

**Preuves Cursiv.**

- [src/core/cursiv/documentation/DocComment.cpp:457](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/documentation/DocComment.cpp#L457) — Ordre et retours de ligne.
- [src/core/cursiv/documentation/DocComment.cpp:505](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/documentation/DocComment.cpp#L505) — Rendu des trois types de liens.
- [src/core/cursiv/documentation/DocComment.cpp:606](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/documentation/DocComment.cpp#L606) — Ordre des hooks et replis.

**Comparaison.** Langium fournit les mêmes styles de tags et les trois tags de liens, mais son renderTag reçoit le nœud structuré avant le rendu des enfants. Le hook Cursiv perd accès aux ranges et à la structure du contenu. Cursiv accepte aussi target|display et les tabulations de séparation, là où ce chemin Langium sépare sur le premier espace.

**Preuves de comparaison.**

- [langium/packages/langium/src/documentation/jsdoc.ts:568](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/documentation/jsdoc.ts#L568) — Hook structuré appelé avant le rendu par défaut.
- [langium/packages/langium/src/documentation/jsdoc.ts:603](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/documentation/jsdoc.ts#L603) — Découpage du lien par espace.

**Limites.** Les chaînes de documentation sont du Markdown auteur et ne subissent pas un échappement général. Les noms exacts pris en charge sont link, linkcode et linkplain écrit «linkplain» dans le code.

<a id="comparison-pub-012"></a>

### PUB-012 — Résolution des liens documentaires

**Question.** Comparer les noms visibles et le format des liens vers une déclaration.

**Cursiv.** La recherche documentaire privilégie les symboles locaux en remontant les parents, puis l’index global, avec égalité de noms sensible à la casse et sans filtrage de type. Elle conserve le document cible via shared_ptr lors d’un lien interdocument, choisit le NameProvider de la langue cible et encode une ancre ligne/colonne 1-based à partir des positions UTF-16. Une description sans node ou un document absent fait revenir au texte, sans tentative de chargement.

**Preuves Cursiv.**

- [src/core/cursiv/documentation/DefaultDocumentationProvider.cpp:58](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/documentation/DefaultDocumentationProvider.cpp#L58) — Résolution et garde du document cible.
- [src/core/cursiv/documentation/DefaultDocumentationProvider.cpp:106](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/documentation/DefaultDocumentationProvider.cpp#L106) — Recherche locale puis globale.

**Comparaison.** Langium suit aussi local puis global, mais utilise directement nameSegment conservé dans AstNodeDescription pour construire le lien. Cursiv exige le nœud vivant et son CST : des index de descriptions externes sans node perdent leurs liens documentaires même si leurs coordonnées pourraient être disponibles.

**Preuves de comparaison.**

- [langium/packages/langium/src/documentation/documentation-provider.ts:53](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/documentation/documentation-provider.ts#L53) — Lien depuis nameSegment sans recharger un AST.
- [langium/packages/langium/src/documentation/documentation-provider.ts:70](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/documentation/documentation-provider.ts#L70) — Recherche des scopes locaux.

**Limites.** La validité du raccourci node suppose le protocole workspace et l’invalidation des index ; un provider externe doit respecter ces garanties.

<a id="comparison-ref-002"></a>

### REF-002 — Adressage structurel d’un nœud

**Question.** Comparer la stabilité et le format des chemins AST utilisables par un service tiers.

**Cursiv.** Le chemin est construit sans récursion à partir des ancêtres et de la réflexion des membres ; le wrapper Document n’entre pas dans le chemin, y compris pour un AST détaché. Un parent qui ne contient pas réellement l’enfant provoque une erreur explicite. La lecture ignore les segments vides, exige un entier uint32 entièrement consommé et sélectionne propriété/index exacts. Les chemins dépendent de l’ordre des collections et ne sont donc pas des identifiants stables après édition structurelle.

**Preuves Cursiv.**

- [src/core/cursiv/workspace/DefaultAstNodeLocator.cpp:29](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultAstNodeLocator.cpp#L29) — Recherche du slot réel de l’enfant.
- [src/core/cursiv/workspace/DefaultAstNodeLocator.cpp:65](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultAstNodeLocator.cpp#L65) — Chemin itératif jusqu’au document/racine détachée.
- [src/core/cursiv/workspace/DefaultAstNodeLocator.cpp:97](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultAstNodeLocator.cpp#L97) — Entier complet contrôlé.

**Comparaison.** Même convention /property@index et chemin racine vide que Langium. Cursiv calcule le slot via la réflexion plutôt que de stocker $containerProperty/$containerIndex. Il refuse les suffixes numériques invalides que parseInt peut accepter dans Langium ; ses parcours de chemin ne consomment pas une pile récursive proportionnelle à la profondeur.

**Preuves de comparaison.**

- [langium/packages/langium/src/workspace/ast-node-locator.ts:40](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/workspace/ast-node-locator.ts#L40) — Chemin récursif et métadonnées de conteneur.
- [langium/packages/langium/src/workspace/ast-node-locator.ts:60](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/workspace/ast-node-locator.ts#L60) — Lecture avec parseInt.

**Limites.** Lecture statique du code, des interfaces et des callers ; ni exécution ni scénario reproduit.

<a id="comparison-ref-003"></a>

### REF-003 — Sélection des symboles exportés

**Question.** Comparer quels nœuds deviennent visibles hors du document et comment un langage peut les sélectionner.

**Cursiv.** Le défaut exporte la racine puis ses enfants directs dans l’ordre de réflexion ; les descendants plus profonds sont exclus par construction. Le hook addExportedSymbol centralise le nom via AstNodeDescriptionProvider. Le helper de parcours accepte un fournisseur d’enfants pour une visibilité différente et vérifie l’annulation par enfant. Aucune duplication interne ni export transitif accidentel trouvé.

**Preuves Cursiv.**

- [src/core/cursiv/references/DefaultScopeComputation.cpp:19](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/references/DefaultScopeComputation.cpp#L19) — Enfants directs seulement.
- [src/core/cursiv/references/DefaultScopeComputation.cpp:35](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/references/DefaultScopeComputation.cpp#L35) — Parcours paramétrable et annulation.
- [src/core/cursiv/references/DefaultScopeComputation.hpp:44](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/references/DefaultScopeComputation.hpp#L44) — Hook de sélection/renommage.

**Comparaison.** Même politique par défaut et mêmes niveaux d’extension que Langium : racine + enfants directs, helper de collecte et hook par symbole. Cursiv remplace Iterable/Promise par callbacks et token synchrones, cohérents avec son scheduler C++.

**Preuves de comparaison.**

- [langium/packages/langium/src/references/scope-computation.ts:93](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/references/scope-computation.ts#L93) — Collecte paramétrable du parent et enfants.
- [langium/packages/langium/src/references/scope-computation.ts:108](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/references/scope-computation.ts#L108) — Hook addExportedSymbol.

**Limites.** Lecture statique du code, des interfaces et des callers ; ni exécution ni scénario reproduit.

<a id="comparison-ref-004"></a>

### REF-004 — Attribution des scopes locaux

**Question.** Comparer l’attribution des scopes à leurs conteneurs et les points d’extension.

**Cursiv.** Chaque nœud nommé est attaché à son parent AST ; la racine et les nœuds sans parent n’entrent pas dans une table sous Document/nullptr. Le résultat est gelé avant retour et affectation au document. Le hook addLocalSymbol peut élargir la visibilité en choisissant un autre conteneur ; il doit respecter le protocole de LocalSymbols décrit dans WSP-022. Le parcours nodes() porte sur l’AST matérialisé et l’annulation précède chaque callback.

**Preuves Cursiv.**

- [src/core/cursiv/references/DefaultScopeComputation.cpp:56](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/references/DefaultScopeComputation.cpp#L56) — Collecte et gel d’une valeur temporaire.
- [src/core/cursiv/references/DefaultScopeComputation.cpp:70](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/references/DefaultScopeComputation.cpp#L70) — Conteneur réel et exclusion de Document.
- [src/core/cursiv/references/DefaultScopeComputation.hpp:50](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/references/DefaultScopeComputation.hpp#L50) — Extension par déclaration.

**Comparaison.** Même rattachement lexical au conteneur et même hook par nœud que Langium. Différence d’intégration : LocalSymbols C++ est gelé et indexé, alors que Langium retourne une MultiMap encore mutable ; les extensions doivent terminer leurs ajouts avant seal.

**Preuves de comparaison.**

- [langium/packages/langium/src/references/scope-computation.ts:117](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/references/scope-computation.ts#L117) — Collecte de tous les descendants dans MultiMap.
- [langium/packages/langium/src/references/scope-computation.ts:133](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/references/scope-computation.ts#L133) — Rattachement au $container.

**Limites.** Lecture statique du code, des interfaces et des callers ; ni exécution ni scénario reproduit.

<a id="comparison-ref-005"></a>

### REF-005 — Résolution d’un nom dans les scopes

**Question.** Comparer priorité locale/globale, homonymes, casse et filtrage par type.

**Cursiv.** Le lookup grimpe du conteneur le plus proche vers la racine, filtre le type accepté et le narrowing avant de retenir un homonyme, puis interroge le global. La politique de casse est résolue une fois et appliquée aux deux niveaux. Un lookup qui atteint le global enregistre le hash même en cas d’échec. Le choix local/global et l’ordre des homonymes sont cohérents ; la portée ASCII du pliage est un écart fonctionnel avec Langium.

**Preuves Cursiv.**

- [src/core/cursiv/references/DefaultScopeProvider.cpp:143](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/references/DefaultScopeProvider.cpp#L143) — Lookup et politique de casse.
- [src/core/cursiv/references/DefaultScopeProvider.cpp:158](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/references/DefaultScopeProvider.cpp#L158) — Premier candidat admissible, pas premier nom brut.
- [src/core/cursiv/references/DefaultScopeProvider.cpp:168](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/references/DefaultScopeProvider.cpp#L168) — Mémorisation avant lookup global.

**Comparaison.** Local avant global et filtrage du type concordent avec DefaultScopeProvider de Langium. Cursiv ajoute un narrowing par site et traite les homonymes globaux en ordre d’URI ; Langium expose des Scope composables et une casse fondée sur toLowerCase. Cet abaissement Unicode de Langium n’est pas non plus un case-fold linguistique complet dépendant de la locale.

**Preuves de comparaison.**

- [langium/packages/langium/src/references/scope-provider.ts:51](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/references/scope-provider.ts#L51) — Composition locale/globale filtrée par type.
- [langium/packages/langium/src/references/scope.ts:80](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/references/scope.ts#L80) — Politique de casse et priorité locale.

**Limites.** Lecture statique du code, des interfaces et des callers ; ni exécution ni scénario reproduit.

<a id="comparison-ref-007"></a>

### REF-007 — Conversion des candidats en résultat de linking

**Question.** Comparer le résultat d’un nom absent, ambigu ou multi-cible et la façon dont le client le consomme.

**Cursiv.** Le linker transforme un nom vide ou absent en erreur, retient le premier candidat simple et collecte tous les candidats multiples. La déduplication commence par un petit tableau puis bascule vers un set avec la même clé document/symbole/nœud ; deux AST créés sur un même CST restent distincts ici. Les résultats peuvent porter un LinkingError, mais leur consommation conserve seulement un état et exige une cible matérialisée.

**Preuves Cursiv.**

- [src/core/cursiv/references/DefaultLinker.cpp:156](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/references/DefaultLinker.cpp#L156) — Nom vide et sélection simple.
- [src/core/cursiv/references/DefaultLinker.cpp:186](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/references/DefaultLinker.cpp#L186) — Déduplication stable par clé complète.
- [src/core/cursiv/syntax-tree/DefaultDocumentReferences.cpp:68](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/syntax-tree/DefaultDocumentReferences.cpp#L68) — Consommation du candidat.
- [src/core/cursiv/syntax-tree/DefaultDocumentReferences.cpp:172](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/syntax-tree/DefaultDocumentReferences.cpp#L172) — Les candidats multiples sans node sont écartés.

**Comparaison.** Absence → erreur, premier homonyme simple et déduplication multiple existent dans les deux frameworks. Langium déduplique par URI+path et peut charger l’AST depuis la description. Cursiv déduplique par document+site+pointeur, permettant plusieurs nœuds sur un même site, mais requiert ce pointeur pour lier.

**Preuves de comparaison.**

- [langium/packages/langium/src/references/linker.ts:269](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/references/linker.ts#L269) — Sélection simple et multiple ; distinct URI/path.
- [langium/packages/langium/src/references/linker.ts:384](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/references/linker.ts#L384) — Chargement depuis URI/path.

**Limites.** Lecture statique du code, des interfaces et des callers ; ni exécution ni scénario reproduit.

<a id="comparison-ref-008"></a>

### REF-008 — Forçage et remise à zéro du linking

**Question.** Comparer la sémantique visible du linking forcé, paresseux et invalidé.

**Cursiv.** link attache le linker/document pour un appel direct puis force des ranges via le scheduler ; le builder évite de modifier ces attachements une fois publiés. unlink remet tous les handles à Unresolved, efface les noms importés puis retire les deux pointeurs atomiques. Les exceptions de provider sont confinées à une référence ; seule l’annulation sort du forçage. Le résultat d’une lecture simple trop précoce n’est pas mémorisé en cas d’échec, tandis qu’une MultiRef garde son échec : ce comportement différent est documenté.

**Preuves Cursiv.**

- [src/core/cursiv/references/DefaultLinker.cpp:123](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/references/DefaultLinker.cpp#L123) — Appel direct et forçage.
- [src/core/cursiv/references/DefaultLinker.cpp:146](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/references/DefaultLinker.cpp#L146) — Remise à zéro complète.
- [src/core/cursiv/syntax-tree/ReferenceResolution.cpp:250](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/syntax-tree/ReferenceResolution.cpp#L250) — Racine d’annulation et confinement par référence.
- [src/core/cursiv/syntax-tree/DocumentReferences.hpp:45](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/syntax-tree/DocumentReferences.hpp#L45) — Asymétrie simple/multiple explicitée.

**Comparaison.** Langium propose également linking forcé/paresseux et unlink. Sa référence simple peut retenter après échec avant ComputedScopes, tandis que sa MultiReference mémorise l’erreur. Cursiv reproduit cette asymétrie mais force par handles en parallèle plutôt que par stream de propriétés AST.

**Preuves de comparaison.**

- [langium/packages/langium/src/references/linker.ts:177](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/references/linker.ts#L177) — Forçage par parcours AST.
- [langium/packages/langium/src/references/linker.ts:256](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/references/linker.ts#L256) — Unlink.
- [langium/packages/langium/src/references/linker.ts:291](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/references/linker.ts#L291) — Résolution simple précoce non mémorisée.
- [langium/packages/langium/src/references/linker.ts:318](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/references/linker.ts#L318) — Résolution multiple mémorisée.

**Limites.** Lecture statique du code, des interfaces et des callers ; ni exécution ni scénario reproduit.

<a id="comparison-svc-001"></a>

### SVC-001 — Ownership des conteneurs de services

**Question.** Comparer la propriété et la durée de vie des services, sans transposer mécaniquement les références JavaScript.

**Cursiv.** Les conteneurs ne sont pas copiables/déplaçables et possèdent leurs slots par unique_ptr ; les défauts empruntent le conteneur stable. SharedCoreServices détruit le registry et donc les langues avant le builder, ce qui conserve les abonnements de caches lors de leur désinscription. Le scheduler et le sink sont déclarés tôt et meurent après leurs utilisateurs. Le conteneur core emprunte shared ; un conteneur personnalisé doit aussi ordonner ses propres slots et ne pas les utiliser depuis un destructeur exécuté plus tard. Les documents peuvent garder un pointeur vers une langue retirée, dont le registry conserve l’ownership.

**Preuves Cursiv.**

- [src/core/cursiv/services/CoreServices.hpp:36](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/services/CoreServices.hpp#L36) — Ownership des slots et conteneur stable.
- [src/core/cursiv/services/SharedCoreServices.hpp:33](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/services/SharedCoreServices.hpp#L33) — Sink et scheduler détruits tard.
- [src/core/cursiv/services/SharedCoreServices.hpp:47](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/services/SharedCoreServices.hpp#L47) — Registry détruit avant builder.
- [src/core/cursiv/services/DefaultCoreService.hpp:15](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/services/DefaultCoreService.hpp#L15) — Dépendances empruntées au conteneur.

**Comparaison.** Langium utilise des références JavaScript possédées indirectement par l’injecteur et le GC, sans l’ordre déterministe de destruction C++. Cursiv rend cet ownership explicite et doit conserver shared plus longtemps que la langue ; le portage des services avec abonnements doit prendre cet ordre en compte. La construction Langium est paresseuse, contre l’installation immédiate des défauts Cursiv.

**Preuves de comparaison.**

- [langium/packages/langium/src/dependency-injection.ts:37](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/dependency-injection.ts#L37) — Injecteur paresseux avec instance mémorisée.
- [langium/packages/langium/src/dependency-injection.ts:122](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/dependency-injection.ts#L122) — Mémorisation dans l’objet du proxy.

**Limites.** Lecture statique du graphe de services et des helpers ; les services personnalisés doivent respecter les contrats d’emprunt et de quiescence.

<a id="comparison-svc-002"></a>

### SVC-002 — Installation des services par défaut

**Question.** Comparer la surcharge d’un service par l’auteur du langage et les effets d’un remplacement sur ses dépendants.

**Cursiv.** Les deux installateurs ne remplissent que les slots nuls : une personnalisation préalable est conservée et un second appel ne remplace rien. Les services par défaut retiennent le conteneur et lisent les siblings à l’usage, ce qui autorise les remplacements du module après l’installation. Parser, reflection et métadonnées restent à la charge du langage ; le filesystem partagé défaut est EmptyFileSystemProvider. Si une allocation échoue, les slots déjà créés restent possédés et une nouvelle installation reprend les slots manquants ; aucun pointeur vers un service remplacé n’est capturé par DefaultCoreService.

**Preuves Cursiv.**

- [src/core/cursiv/services/CoreServices.cpp:62](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/services/CoreServices.cpp#L62) — Fill-if-empty des services core.
- [src/core/cursiv/services/SharedCoreServices.cpp:44](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/services/SharedCoreServices.cpp#L44) — Fill-if-empty shared.
- [src/core/cursiv/services/DefaultCoreService.hpp:7](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/services/DefaultCoreService.hpp#L7) — Lecture tardive des siblings.
- [src/core/cursiv/services/CoreServices.hpp:99](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/services/CoreServices.hpp#L99) — Parser/réflexion/métadonnées à fournir.

**Comparaison.** Cursiv personnalise par affectation de unique_ptr dans le module ; Langium fusionne des factories avant création paresseuse. Les deux permettent de remplacer sélectivement les défauts, mais le code Cursiv doit éviter de mémoriser des pointeurs de siblings lors de la construction si un module les remplace ensuite.

**Preuves de comparaison.**

- [langium/packages/langium/src/default-module.ts:50](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/default-module.ts#L50) — Factories du module core par défaut.
- [langium/packages/langium/src/default-module.ts:109](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/default-module.ts#L109) — Factories du module shared.
- [langium/packages/langium/src/dependency-injection.ts:61](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/dependency-injection.ts#L61) — Fusion des modules avant injection.

**Limites.** Lecture statique du graphe de services et des helpers ; les services personnalisés doivent respecter les contrats d’emprunt et de quiescence.

<a id="comparison-svc-003"></a>

### SVC-003 — Construction typée d’un langage

**Question.** Comparer l’effort pour ajouter des services typés à un langage et à son workspace.

**Cursiv.** CoreServicesFor utilise une base CoreServices virtuelle et bindShared dans son constructeur, conservant une seule base lors des compositions core/LSP. Les helpers exigent statiquement le bon héritage et un constructeur TShared& ; create_language conserve le shared et dépose la langue dans le registry avant de rendre sa référence. register_language récupère TShared par dynamic_cast de référence : une incompatibilité produit bad_cast avant création de la langue. Ces helpers maintiennent la même identité shared ; la porte erased du registry est auditée séparément.

**Preuves Cursiv.**

- [src/core/cursiv/services/CoreServicesFor.hpp:25](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/services/CoreServicesFor.hpp#L25) — Base virtuelle, référence typed et bindShared.
- [src/core/cursiv/services/CoreServicesFor.hpp:41](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/services/CoreServicesFor.hpp#L41) — Contraintes statiques du constructeur.
- [src/core/cursiv/services/CoreLanguage.hpp:38](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/services/CoreLanguage.hpp#L38) — Ownership shared et référence stable de langue.
- [src/core/cursiv/services/CoreLanguage.hpp:55](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/services/CoreLanguage.hpp#L55) — dynamic_cast de référence.

**Comparaison.** Langium étend ses ensembles par intersections de types et modules typés ; Cursiv utilise des conteneurs dérivés et des helpers template, avec un point de vérification dynamique pour l’inscription dans un workspace effacé. L’effort C++ inclut des constructeurs et l’ordre de vie des slots, mais shared reste directement typé dans les extensions.

**Preuves de comparaison.**

- [langium/packages/langium/src/services.ts:100](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/services.ts#L100) — Intersection des services générés et défauts.
- [langium/packages/langium/src/services.ts:135](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/services.ts#L135) — Composition des services shared.
- [langium/packages/langium/src/dependency-injection.ts:58](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/dependency-injection.ts#L58) — Injection typée de plusieurs modules.

**Limites.** Lecture statique du graphe de services et des helpers ; les services personnalisés doivent respecter les contrats d’emprunt et de quiescence.

<a id="comparison-svc-004"></a>

### SVC-004 — Contrôle des slots requis

**Question.** Comparer quand les dépendances requises et les cycles de construction deviennent observables au client.

**Cursiv.** first_missing_service consulte le visiteur virtuel, donc peut inclure les slots requis d’une classe dérivée. Les slots optionnels tels grammarSource n’empêchent pas l’inscription ; le registry refuse un shared/core incomplet et un languageId vide avant transfert dans _owned. Le mécanisme dépend toutefois de déclarations de slots correctes et ne détecte ni les cycles de construction, ni l’identité du shared auquel la langue a été attachée.

**Preuves Cursiv.**

- [src/core/cursiv/services/ServiceSlot.hpp:23](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/services/ServiceSlot.hpp#L23) — Scan du visiteur virtuel.
- [src/core/cursiv/services/CoreServices.cpp:34](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/services/CoreServices.cpp#L34) — Liste des slots et option grammarSource.
- [src/core/cursiv/services/DefaultServiceRegistry.cpp:24](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/services/DefaultServiceRegistry.cpp#L24) — Contrôles avant ownership.

**Comparaison.** Cursiv identifie une dépendance manquante lors de l’inscription par nom de slot, mais les constructeurs personnalisés doivent éviter de déréférencer un slot encore vide avant ce contrôle. Langium résout les dépendances à la première demande et détecte un cycle de factories avec __requested__. Langium ne valide pas non plus l’appartenance shared lors du register brut ; cette restriction C++ est néanmoins plus sensible à la destruction.

**Preuves de comparaison.**

- [langium/packages/langium/src/dependency-injection.ts:122](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/dependency-injection.ts#L122) — Erreur de cycle/échec lors de la résolution.
- [langium/packages/langium/src/service-registry.ts:61](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/service-registry.ts#L61) — Register utilise les métadonnées sans contrôle shared.

**Limites.** Lecture statique du graphe de services et des helpers ; les services personnalisés doivent respecter les contrats d’emprunt et de quiescence.

<a id="comparison-svc-006"></a>

### SVC-006 — Sélection des services d’un document

**Question.** Comparer choix du langage par URI/languageId et conflits d’enregistrement.

**Cursiv.** findServices tente un languageId explicitement enregistré, sinon le nom de fichier exact, sinon l’extension ; getServices réutilise exactement cette décision et distingue registry vide/URI sans langage. hasServices est la variante URI seule et ne peut donc prédire une demande avec languageId explicite. Les extensions sont normalisées à l’inscription et les clés vides ignorées ; les noms exacts gardent leur casse. Un ID explicite inconnu retombe sur l’URI plutôt que de faire échouer immédiatement, conforme au comportement Langium lu.

**Preuves Cursiv.**

- [src/core/cursiv/services/DefaultServiceRegistry.cpp:94](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/services/DefaultServiceRegistry.cpp#L94) — Priorité ID reconnu, nom, extension.
- [src/core/cursiv/services/DefaultServiceRegistry.cpp:104](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/services/DefaultServiceRegistry.cpp#L104) — Issues explicites de getServices.
- [src/core/cursiv/services/DefaultServiceRegistry.cpp:79](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/services/DefaultServiceRegistry.cpp#L79) — Normalisation des extensions.
- [src/core/cursiv/services/DefaultServiceRegistry.hpp:34](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/services/DefaultServiceRegistry.hpp#L34) — hasServices ne reçoit pas languageId.

**Comparaison.** La priorité est la même que Langium 4.4.0 : ID d’un buffer reconnu, nom exact, extension. Cursiv reçoit cet ID comme argument core ; Langium consulte TextDocumentProvider. Le commentaire Langium évoque un fallback si un seul langage est enregistré, mais son implémentation 4.4.0 ne le fait pas ; aucun écart de comportement effectif n’est donc attribué à Cursiv sur ce point.

**Preuves de comparaison.**

- [langium/packages/langium/src/service-registry.ts:80](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/service-registry.ts#L80) — Ordre effectif des recherches.
- [langium/packages/langium/src/service-registry.ts:23](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/service-registry.ts#L23) — Commentaire du fallback mono-langage non retrouvé dans le code.

**Limites.** Lecture statique du graphe de services et des helpers ; les services personnalisés doivent respecter les contrats d’emprunt et de quiescence.

<a id="comparison-txt-005"></a>

### TXT-005 — Conversion offset octets vers position éditeur

**Question.** Comparer les positions exposées à l’éditeur et leurs bornes, notamment UTF-16 face aux offsets octets.

**Cursiv.** positionAt borne l’offset à la taille et à la fin du contenu de ligne ; offsetAt borne ligne/colonne et compte les unités UTF-16. Le chemin ASCII est direct ; le chemin par mots de huit octets exige une frontière de scalaire. Une position au milieu d’une paire de surrogates est arrondie à la fin du scalaire UTF-8, car l’offset octet ne représente pas une demi-unité UTF-16. Cursor conserve sa progression de décodage pour les appels monotones, y compris plusieurs offsets internes au même scalaire.

**Preuves Cursiv.**

- [src/core/cursiv/workspace/TextDocument.cpp:142](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/TextDocument.cpp#L142) — Conversions et clamping.
- [src/core/cursiv/workspace/TextDocument.cpp:329](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/TextDocument.cpp#L329) — Curseur monotone.
- [src/lsp/cursiv/text/LspPosition.hpp:20](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/lsp/cursiv/text/LspPosition.hpp#L20) — Copie des coordonnées UTF-16 au passage LSP.

**Comparaison.** Les positions LSP ont la même unité UTF-16. Dans Langium, le gestionnaire normalisé délègue create/update au TextDocument de sa configuration ; les offsets JavaScript sont déjà des unités UTF-16. Un client qui transpose directement des offsets internes Langium vers Cursiv doit les convertir en offsets UTF-8.

**Preuves de comparaison.**

- [langium/packages/langium/src/lsp/normalized-text-documents.ts:140](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/lsp/normalized-text-documents.ts#L140) — Configuration du gestionnaire de documents.
- [langium/packages/langium/src/utils/cst-utils.ts:77](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/utils/cst-utils.ts#L77) — Conversion des positions Chevrotain en positions LSP.

**Limites.** Une identité aller-retour n’est pas promise pour des positions internes à un scalaire ou à un terminateur. La borne globale de taille est traitée en TXT-004.

<a id="comparison-txt-008"></a>

### TXT-008 — Construction de texte et indentation

**Question.** Comparer indentation et composition de texte ; la provenance des fragments relève séparément d’API-003.

**Cursiv.** L’indentation n’est ajoutée qu’au début de ligne et plafonnée par maxIndent. Chaque fin de ligne élimine les blancs terminaux ; cut_from recalcule si une ligne est commencée, et take remet le contenu à zéro tout en conservant le niveau d’indentation courant. Les scopes Indented restaurent le niveau à la sortie ; les jointures empruntent leur séquence pour un usage immédiat. Les deux conversions numériques disposent de vingt caractères, suffisants pour les entiers 64 bits.

**Preuves Cursiv.**

- [src/core/cursiv/text/TextBuilder.cpp:16](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/text/TextBuilder.cpp#L16) — Extraction et état de ligne.
- [src/core/cursiv/text/TextBuilder.cpp:56](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/text/TextBuilder.cpp#L56) — Indentation et fin de ligne.
- [src/core/cursiv/text/TextBuilder.hpp:1](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/text/TextBuilder.hpp#L1) — Composition, jointure et garde d’indentation.

**Comparaison.** Langium compose un arbre de GeneratorNode avec IndentNode, indentation configurable et traitement spécifique des lignes vides. Cursiv assemble immédiatement une chaîne avec indentation en espaces et nettoyage des fins de ligne ; une génération qui dépend de tabulations, de blancs conservés ou de réutilisation d’un arbre doit adapter ses templates.

**Preuves de comparaison.**

- [langium/packages/langium/src/generate/generator-node.ts:409](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/generate/generator-node.ts#L409) — Création/configuration de IndentNode.

**Limites.** L’ajout de texte peut normaliser ses blancs terminaux ; ce n’est pas un outil de copie byte-for-byte. Provenance examinée en API-003.

<a id="comparison-val-001"></a>

### VAL-001 — Enregistrement et indexation des checks

**Question.** Comparer catégories, héritage de types et enregistrement de checks personnalisés.

**Cursiv.** Les catégories nulles/built_in et les types absents de la réflexion sont rejetés avant l’enregistrement. Un check de base partage une même closure possédée entre toutes les lignes de sous-types ; les captures du helper pointent vers le service validateur, dont les temporaires sont refusés. L’inscription se fait au bootstrap, sans concurrence avec les lecteurs. La recherche de multiplicateur sans collisions possède cependant une rupture de garantie d’exception.

**Preuves Cursiv.**

- [src/core/cursiv/validation/DefaultValidationRegistry.cpp:170](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/validation/DefaultValidationRegistry.cpp#L170) — Validation des catégories et familles.
- [src/core/cursiv/validation/DefaultValidationRegistry.cpp:211](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/validation/DefaultValidationRegistry.cpp#L211) — Closure partagée entre les sous-types.
- [src/core/cursiv/validation/ValidationRegistry.hpp:144](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/validation/ValidationRegistry.hpp#L144) — Inscription avant lectures concurrentes.
- [src/core/cursiv/validation/ValidationRegistry.hpp:159](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/validation/ValidationRegistry.hpp#L159) — Validator emprunté ; rvalue refusée.

**Comparaison.** Les deux registres réservent built-in, développent les sous-types et exécutent les checks spécifiques avant AstNode. Cursiv ajoute exactTypeOnly et contrôle le type absent ; ses catégories sont des identités par adresse et son inscription est figée au bootstrap, contre des chaînes et closures JavaScript dans Langium.

**Preuves de comparaison.**

- [langium/packages/langium/src/validation/validation-registry.ts:156](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/validation/validation-registry.ts#L156) — Catégories réservées et inscription.
- [langium/packages/langium/src/validation/validation-registry.ts:205](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/validation/validation-registry.ts#L205) — Développement des sous-types et catch-all.

**Limites.** Analyse statique uniquement ; les branches d’exception et coûts structurels ne sont pas des mesures ni des reproductions.

<a id="comparison-val-003"></a>

### VAL-003 — Pipeline documentaire et ordre des diagnostics

**Question.** Comparer quelles erreurs précèdent ou empêchent les validations métier et la visibilité des diagnostics.

**Cursiv.** La passe ajoute aux diagnostics reçus : built_in commande parse/linking ; les stops ne concernent que cette moitié. Une passe filtrée de checks n’est donc pas stoppée par d’anciens diagnostics de parse. L’arrêt sur linking se fonde sur les diagnostics nouvellement ajoutés. Le tri emploie (offset,position initiale), préserve les égalités et inclut les diagnostics préexistants. Les options d’arrêt sont explicitement réservées aux appels directs, le builder les laisse à false pour ne pas marquer comme exécutées des catégories sautées.

**Preuves Cursiv.**

- [src/core/cursiv/validation/DefaultDocumentValidator.cpp:83](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/validation/DefaultDocumentValidator.cpp#L83) — Filtrage des phases et arrêts.
- [src/core/cursiv/validation/DefaultDocumentValidator.cpp:122](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/validation/DefaultDocumentValidator.cpp#L122) — Permutation stable sur position initiale.
- [src/core/cursiv/validation/DocumentValidator.hpp:47](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/validation/DocumentValidator.hpp#L47) — Options de stop hors build incrémental.

**Comparaison.** Langium possède aussi les trois séquences builtin puis checks et les stops, mais distingue les erreurs de lexer et de parser ; Cursiv n’expose pas stopAfterLexingErrors dans ce parser sans lexer séparé. Cursiv enrichit un vecteur existant et trie globalement, tandis que Langium retourne un nouveau tableau. Une liste Cursiv vide signifie toutes catégories ; une liste Langium [] filtre tous les checks/builtin, donc le portage doit distinguer vide et non spécifié.

**Preuves de comparaison.**

- [langium/packages/langium/src/validation/document-validator.ts:77](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/validation/document-validator.ts#L77) — Nouveau tableau ; catégories absentes vs tableau fourni.
- [langium/packages/langium/src/validation/validation-registry.ts:215](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/validation/validation-registry.ts#L215) — Tableau vide conserve un filtre qui ne sélectionne rien.

**Limites.** Analyse statique uniquement ; les branches d’exception et coûts structurels ne sont pas des mesures ni des reproductions.

<a id="comparison-val-006"></a>

### VAL-006 — Hooks avant et après validation AST

**Question.** Comparer les possibilités de pré/post-traitement et leurs garanties en cas d’erreur.

**Cursiv.** Les hooks before/after sont filtrés par catégorie et exécutés dans l’ordre d’inscription autour du parcours. Les hooks seuls fonctionnent aussi sans AST. Une exception métier est convertie via handleException sans interrompre les autres hooks ; l’annulation ou une surcharge de handleException qui relance sort de la passe. after n’est donc pas un finally ni un contrat de nettoyage garanti ; les ressources temporaires doivent reposer sur RAII ou être réinitialisées au before suivant.

**Preuves Cursiv.**

- [src/core/cursiv/validation/DefaultDocumentValidator.cpp:326](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/validation/DefaultDocumentValidator.cpp#L326) — Exécution même pour un langage n’ayant que des hooks.
- [src/core/cursiv/validation/DefaultDocumentValidator.cpp:352](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/validation/DefaultDocumentValidator.cpp#L352) — Ordre before→nodes→after sans finally.
- [src/core/cursiv/validation/DefaultDocumentValidator.cpp:308](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/validation/DefaultDocumentValidator.cpp#L308) — Catégories et politique d’exception des hooks.
- [src/core/cursiv/validation/DefaultDocumentValidator.cpp:493](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/validation/DefaultDocumentValidator.cpp#L493) — After vérifie l’annulation.

**Comparaison.** Langium propose les mêmes hooks et ne garantit pas non plus after en cas d’annulation. Il transmet rootNode et la liste des catégories à un hook non catégorisé ; Cursiv reçoit le Document, attribue une catégorie lors de l’inscription et filtre avant appel. Un hook Langium multi-catégorie doit être adapté et l’état partagé doit accepter la vraie concurrence C++.

**Preuves de comparaison.**

- [langium/packages/langium/src/validation/document-validator.ts:197](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/validation/document-validator.ts#L197) — Séquence sans finally.
- [langium/packages/langium/src/validation/document-validator.ts:210](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/validation/document-validator.ts#L210) — Root et catégories transmis aux hooks.
- [langium/packages/langium/src/validation/validation-registry.ts:240](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/validation/validation-registry.ts#L240) — Inscription du hook sans catégorie.

**Limites.** Analyse statique uniquement ; les branches d’exception et coûts structurels ne sont pas des mesures ni des reproductions.

<a id="comparison-val-007"></a>

### VAL-007 — Sélection et élagage des nœuds à valider

**Question.** Comparer les exclusions de nœuds et de sous-arbres disponibles pour un langage.

**Cursiv.** Quand asksNodeOptions est activé, les options sont collectées avant les checks, ce qui rend l’élagage indépendant de l’ordre enfants/parents de nodes(). validateNode=false n’exclut que le nœud ; validateChildren=false exclut ses descendants, via une mémoïsation des chaînes d’ancêtres. Le parcours reste séquentiel dans ce mode, et les ensembles/path ne sont pas partagés entre workers. Les options sont néanmoins demandées à tous les nœuds, même sous un sous-arbre qui sera écarté.

**Preuves Cursiv.**

- [src/core/cursiv/validation/DefaultDocumentValidator.cpp:398](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/validation/DefaultDocumentValidator.cpp#L398) — Précollecte des options.
- [src/core/cursiv/validation/DefaultDocumentValidator.cpp:420](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/validation/DefaultDocumentValidator.cpp#L420) — Mémo des ancêtres et distinction nœud/enfants.
- [src/core/cursiv/validation/DefaultDocumentValidator.cpp:464](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/validation/DefaultDocumentValidator.cpp#L464) — Mode d’options séquentiel.

**Comparaison.** Même pouvoir de refuser un nœud ou ses descendants. Langium parcourt en préordre et prune immédiatement, sans consulter les descendants refusés ; Cursiv consulte toutes les options avant un parcours de stockage et désactive la parallélisation de ce parcours. Le nombre d’appels au hook diffère sur les sous-arbres élagués.

**Preuves de comparaison.**

- [langium/packages/langium/src/validation/document-validator.ts:245](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/validation/document-validator.ts#L245) — Options puis prune direct de l’itérateur.

**Limites.** Analyse statique uniquement ; les branches d’exception et coûts structurels ne sont pas des mesures ni des reproductions.

<a id="comparison-wsp-001"></a>

### WSP-001 — Création d’un document et choix de langage

**Question.** Comparer les entrées texte/URI et l’état du document retourné ; les modèles synthétiques relèvent d’API-002.

**Cursiv.** fromTextDocument normalise l’URI documentaire, choisit les services avec le languageId client, construit le Document propriétaire du TextDocument puis parse ses propres octets. La racine reçoit le Document comme parent avant publication de Parsed. fromString sélectionne la langue par URI, crée un TextDocument version 0 et délègue au même chemin ; fromUri lit une fois puis délègue. La factory ne stocke pas le document : un échec de parse détruit l’objet local sans publier une entrée dans Documents.

**Preuves Cursiv.**

- [src/core/cursiv/workspace/DefaultDocumentFactory.cpp:51](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultDocumentFactory.cpp#L51) — Ordre normalisation/services/création/parse.
- [src/core/cursiv/workspace/DefaultDocumentFactory.cpp:35](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultDocumentFactory.cpp#L35) — AST attaché avant état Parsed.
- [src/core/cursiv/workspace/DefaultDocumentFactory.cpp:62](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultDocumentFactory.cpp#L62) — Délégation des entrées texte et URI.
- [src/core/cursiv/workspace/DocumentFactory.cpp:13](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DocumentFactory.cpp#L13) — Document détient le TextDocument déplacé.

**Comparaison.** Les entrées texte/URI des deux frameworks produisent un document Parsed sans l’installer automatiquement dans le store. Langium propose versions synchrones/asynchrones et garde ou construit paresseusement TextDocument ; Cursiv possède immédiatement le texte dont son AST peut emprunter les octets et expose un appel C++ synchrone avec token.

**Preuves de comparaison.**

- [langium/packages/langium/src/workspace/documents.ts:183](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/workspace/documents.ts#L183) — Entrées URI et TextDocument.
- [langium/packages/langium/src/workspace/documents.ts:217](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/workspace/documents.ts#L217) — Création et parsing.
- [langium/packages/langium/src/workspace/documents.ts:253](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/workspace/documents.ts#L253) — Document à l’état Parsed.

**Limites.** La normalisation primitive et les règles de registry sont auditées séparément ; des services personnalisés peuvent élargir les préconditions.

<a id="comparison-wsp-008"></a>

### WSP-008 — Exécution et publication des étapes d’une phase

**Question.** Comparer ce qu’un état publié garantit effectivement à ses consommateurs.

**Cursiv.** runPhase capture l’état d’entrée, exécute chaque Step nécessaire puis publie l’état seulement après retour normal du body. Les waiters documentaires sont réveillés à ce point ; la lecture des structures exige encore le régime du workspace. Lorsqu’un beforePublish est requis, les notifications sont différées, le raccord de publication est exécuté même après échec des workers et seules les étapes réellement franchies sont notifiées. L’exception est relancée avant publication du jalon global. En absence de travail, la phase contrôle quand même le token et publie son jalon global avec une liste vide.

**Preuves Cursiv.**

- [src/core/cursiv/workspace/DefaultDocumentBuilder.cpp:91](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultDocumentBuilder.cpp#L91) — Body avant état et réveil documentaire.
- [src/core/cursiv/workspace/DefaultDocumentBuilder.cpp:108](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultDocumentBuilder.cpp#L108) — Échec conservé pour beforePublish.
- [src/core/cursiv/workspace/DefaultDocumentBuilder.cpp:117](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultDocumentBuilder.cpp#L117) — Notification des seules étapes réellement atteintes.
- [src/core/cursiv/workspace/DefaultDocumentBuilder.cpp:129](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultDocumentBuilder.cpp#L129) — Échec/annulation avant jalons globaux.

**Comparaison.** Langium fait avancer les documents séquentiellement avec runCancelable et notifie chaque document, puis la phase. Cursiv parallélise par document et distingue state atomique, publication globale et gate de lecture. Son jalon global est publié avant les callbacks de phase, autorisant leur attente du jalon courant.

**Preuves de comparaison.**

- [langium/packages/langium/src/workspace/document-builder.ts:492](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/workspace/document-builder.ts#L492) — Boucle runCancelable et notifications.
- [langium/packages/langium/src/workspace/document-builder.ts:507](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/workspace/document-builder.ts#L507) — Notification globale puis currentState.

**Limites.** Un listener qui lève peut laisser un état documentaire atteint mais un jalon global non publié ; la garantie de cet état porte sur son body, pas sur tous les listeners.

<a id="comparison-wsp-011"></a>

### WSP-011 — Attente de l’état d’un document

**Question.** Comparer les issues d’une attente et le traitement d’un document exclu ou supprimé.

**Cursiv.** L’attente récupère une fois le shared_ptr sous read Immediate, relâche le workspace puis attend uniquement sur state atomique et bookkeeping du builder. Le handle garde l’objet vivant, sans bloquer son reset. Le compteur de waiters est installé avant le prédicat ; un worker qui voit ce compteur prend stateMutex avant notify_all, fermant le trou test/sommeil. Le prédicat distingue document atteint, tentative échouée et jalon global passé alors que le document reste en retard. Un objet retiré/recréé sous le même id reste l’ancien objet capturé : il peut être déclaré Unreachable, ce qui oblige la route à reconsidérer sa requête.

**Preuves Cursiv.**

- [src/core/cursiv/workspace/DefaultDocumentBuilder.cpp:794](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultDocumentBuilder.cpp#L794) — Handle acquis hors de l’attente.
- [src/core/cursiv/workspace/DefaultDocumentBuilder.cpp:810](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultDocumentBuilder.cpp#L810) — Token et compteur avant prédicat.
- [src/core/cursiv/workspace/DefaultDocumentBuilder.cpp:820](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultDocumentBuilder.cpp#L820) — Évaluation des trois issues.
- [src/core/cursiv/workspace/DefaultDocumentBuilder.cpp:745](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultDocumentBuilder.cpp#L745) — Handshake de réveil.

**Comparaison.** Cursiv retourne des WaitOutcome explicites et peut déclarer Unreachable après passage du jalon global. Langium rejette par ResponseError pour absence ou exclusion constatée et écoute ensuite les événements documentaires par URI ; Cursiv capture un objet par id avec shared_ptr, ce qui change le cas de suppression/recréation pendant l’attente.

**Preuves de comparaison.**

- [langium/packages/langium/src/workspace/document-builder.ts:543](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/workspace/document-builder.ts#L543) — Absence/exclusion et résultat d’attente.
- [langium/packages/langium/src/workspace/document-builder.ts:570](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/workspace/document-builder.ts#L570) — Abonnement à l’événement documentaire par URI.

**Limites.** Une annulation de build seule n’est pas une terminaison de waitUntil ; voir WSP-026. Reached ne dispense pas de relire phase/identité sous verrou après l’attente.

<a id="comparison-wsp-012"></a>

### WSP-012 — Abonnements et livraison des événements de build

**Question.** Comparer événements et désabonnement, ainsi que les hypothèses d’ordre et de réentrance des callbacks.

**Cursiv.** Les trois listes de listeners sont des vecteurs de pointeurs lus sans mutex pendant le build. Le contrat limite expressément ajout/retrait à la configuration hors build, donc l’absence de copie n’est pas un défaut pour un désabonnement conforme. Les listeners documentaires sont appelés depuis les workers, ceux de phase depuis le thread pilote après publication globale ; les updates précèdent les phases. OperationCancelled est absorbée pour laisser les listeners documentaires suivants être appelés, tandis qu’une autre exception interrompt leur liste et échoue la tentative.

**Preuves Cursiv.**

- [src/core/cursiv/workspace/DocumentBuilder.hpp:27](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DocumentBuilder.hpp#L27) — Thread et moment des listeners de phase.
- [src/core/cursiv/workspace/DocumentBuilder.hpp:44](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DocumentBuilder.hpp#L44) — Thread et interdiction d’attendre le jalon global depuis un worker.
- [src/core/cursiv/workspace/DocumentBuilder.hpp:70](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DocumentBuilder.hpp#L70) — Ajout/retrait réservés à la configuration.
- [src/core/cursiv/workspace/DefaultDocumentBuilder.cpp:212](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultDocumentBuilder.cpp#L212) — Traitement des exceptions de listeners.

**Comparaison.** Langium retourne des Disposable et notifie une copie des callbacks documentaires ; Cursiv impose des objets vivants et une configuration hors build. Cette contrainte de désabonnement et le parallélisme par document sont des adaptations nécessaires pour les extensions, pas un port transparent des callbacks JS.

**Preuves de comparaison.**

- [langium/packages/langium/src/workspace/document-builder.ts:511](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/workspace/document-builder.ts#L511) — Enregistrement et Disposable.
- [langium/packages/langium/src/workspace/document-builder.ts:606](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/workspace/document-builder.ts#L606) — Copie des listeners avant notification.

**Limites.** Une extension se désabonnant ou détruisant un autre abonné pendant la livraison viole le contrat actuel ; aucune sécurité n’est promise pour cet usage.

<a id="comparison-wsp-015"></a>

### WSP-015 — Frontière durable et annulation d’une mutation

**Question.** Comparer la frontière d’annulation d’une mutation et ce qu’une lecture autorisée peut observer.

**Cursiv.** Durable empêche la cancellation de la mutation tant qu’elle n’a pas passé settle. settle n’est accepté que sur le thread writer actif, devient idempotent, autorise les Immediate puis rend la durabilité : présence d’un writer plus récent ou cancelWrite différé déclenche l’arrêt après sortie du mutex. Sans settle explicite, la fin de write rend aussi ce guard. La sûreté des arbres repose sur le contrat que les mutations destructrices sont finies avant cette frontière ; les phases ultérieures doivent seulement faire progresser les documents admis.

**Preuves Cursiv.**

- [src/core/cursiv/workspace/DefaultWorkspaceLock.cpp:173](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultWorkspaceLock.cpp#L173) — Handoff de durabilité.
- [src/core/cursiv/workspace/DefaultWorkspaceLock.cpp:185](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultWorkspaceLock.cpp#L185) — Contrôle du propriétaire, idempotence et stop hors mutex.
- [src/core/cursiv/workspace/DefaultWorkspaceLock.cpp:206](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultWorkspaceLock.cpp#L206) — Annulation différée d’un latest Durable.
- [src/core/cursiv/workspace/WorkspaceLock.hpp:60](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/WorkspaceLock.hpp#L60) — Contrat interdisant les destructions après settle.

**Comparaison.** Langium permet Immediate en parallèle d’une write async, suit ces reads avant la prochaine write et annule la précédente action à l’enqueue. Cursiv ajoute une frontière native settle et le mode Durable avant lequel Immediate reste fermé ; porter une action async implique de choisir explicitement sa phase de mutation et ses durées de vie.

**Preuves de comparaison.**

- [langium/packages/langium/src/workspace/workspace-lock.ts:72](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/workspace/workspace-lock.ts#L72) — Annulation à l’enqueue.
- [langium/packages/langium/src/workspace/workspace-lock.ts:79](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/workspace/workspace-lock.ts#L79) — Immediate contourne la queue.
- [langium/packages/langium/src/workspace/workspace-lock.ts:115](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/workspace/workspace-lock.ts#L115) — Prochaine write attend les Immediate.

**Limites.** La primitive ne peut détecter toutes les mutations illégales d’une extension après settle ; les assertions du builder et les états demandés par les routes sont examinés séparément.

<a id="comparison-wsp-019"></a>

### WSP-019 — Détection des documents affectés

**Question.** Comparer les modifications qui imposent un nouveau linking, y compris les noms auparavant introuvables.

**Cursiv.** Les noms consultés au-delà du local sont indexés même si aucun symbole ne résout ; les exports anciens et nouveaux marquent leurs importateurs. Ce marquage persiste au retrait des références et disparaît après mise à jour réussie. Le masque à 64 bits est uniquement un filtre négatif : toute collision est confirmée par les IDs des arêtes, et les arêtes locales ne créent pas d’auto-affectation. Aucun faux négatif trouvé avec les ScopeProvider/ReferenceDescriptionProvider par défaut.

**Preuves Cursiv.**

- [src/core/cursiv/workspace/DefaultIndexManager.cpp:141](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultIndexManager.cpp#L141) — Marquage des noms disparus et apparus.
- [src/core/cursiv/workspace/DefaultIndexManager.cpp:243](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultIndexManager.cpp#L243) — Nom hashé → documents qui l’ont consulté.
- [src/core/cursiv/workspace/DefaultIndexManager.cpp:255](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultIndexManager.cpp#L255) — Marquage prioritaire même avec changed vide.
- [src/core/cursiv/workspace/DefaultIndexManager.cpp:271](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultIndexManager.cpp#L271) — Confirmation par ID exact.

**Comparaison.** Langium DefaultIndexManager.isAffected regarde seulement les cibles interdocuments ; son builder traite séparément les références en erreur. Cursiv ajoute le suivi des noms consultés et peut aussi relier un nom déjà résolu vers un nouvel homonyme prioritaire. Un ScopeProvider personnalisé doit préserver cet enregistrement ou fournir sa propre politique d’affectation.

**Preuves de comparaison.**

- [langium/packages/langium/src/workspace/index-manager.ts:187](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/workspace/index-manager.ts#L187) — isAffected parcourt uniquement les références ciblant une URI changée.

**Limites.** Lecture statique des sources et contrats ; aucune exécution, injection d’allocation défaillante ni mesure de contention.

<a id="comparison-wsp-025"></a>

### WSP-025 — Initialisation et disponibilité du workspace

**Question.** Comparer les préconditions auxquelles les requêtes utilisateur peuvent considérer le workspace prêt.

**Cursiv.** L’initialisation installe d’abord les documents additionnels puis ceux des dossiers sans écraser les objets déjà présents. Le scan est volontairement non annulable et le token n’est contrôlé qu’avant le build. ready est résolu une seule fois avant le build, y compris dans le catch du chargement : il indique la fin de disponibilité du scan, pas une validation réussie. Le build porte sur tous les documents détenus ; les anciens documents avec erreurs de linking sont réinitialisés au niveau scopes pour profiter des symboles nouvellement arrivés.

**Preuves Cursiv.**

- [src/core/cursiv/workspace/DefaultWorkspaceManager.cpp:134](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultWorkspaceManager.cpp#L134) — Racines et protection des documents existants.
- [src/core/cursiv/workspace/DefaultWorkspaceManager.cpp:143](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultWorkspaceManager.cpp#L143) — ready résolu avant build et une seule fois.
- [src/core/cursiv/workspace/DefaultWorkspaceManager.cpp:163](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultWorkspaceManager.cpp#L163) — Scan délibérément non annulable.
- [src/core/cursiv/workspace/DefaultWorkspaceManager.cpp:183](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultWorkspaceManager.cpp#L183) — Échec de chargement et reprise du build global.

**Comparaison.** Langium fait également un startup non annulable et résout ready avant le build. Cursiv résout aussi ready sur exception de chargement, tandis que la résolution Langium se trouve à la fin normale de performStartup. Un hôte doit traiter ready comme une barrière de disponibilité distincte du résultat d’initialisation et de l’état Validated.

**Preuves de comparaison.**

- [langium/packages/langium/src/workspace/workspace-manager.ts:132](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/workspace/workspace-manager.ts#L132) — Annulation après startup puis build.
- [langium/packages/langium/src/workspace/workspace-manager.ts:144](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/workspace/workspace-manager.ts#L144) — Startup et résolution ready à la fin normale.

**Limites.** Un ready résolu ne prouve pas chargement intégral ni absence d’erreur : le caller doit observer l’exception/l’observabilité et attendre la phase requise. Les appels concurrents à initializeWorkspace sont hors régime write prévu.

<a id="comparison-wsp-026"></a>

### WSP-026 — Attente d’une publication du workspace

**Question.** Comparer si l’attente concerne un état courant, une publication ou une tentative de build précise.

**Cursiv.** beginAttempt redescend currentState à Changed et incrémente le numéro de tentative avant le moindre callback/mutation. waitUntil global capture le compteur de publications du jalon ; il peut répondre Reached si ce jalon a été publié puis redescendu avant son réveil, sans confondre une ancienne publication antérieure à l’attente. Les exceptions ordinaires enregistrent failedAttempt et réveillent ; la comparaison par numéro ne transporte pas un échec ancien dans une nouvelle tentative. OperationCancelled ne publie aucune fin d’attente : le mécanisme compte sur une prochaine tentative ou le token du waiter.

**Preuves Cursiv.**

- [src/core/cursiv/workspace/DefaultDocumentBuilder.cpp:724](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultDocumentBuilder.cpp#L724) — Distinction annulation/échec.
- [src/core/cursiv/workspace/DefaultDocumentBuilder.cpp:737](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultDocumentBuilder.cpp#L737) — Tentative et état initial.
- [src/core/cursiv/workspace/DefaultDocumentBuilder.cpp:772](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultDocumentBuilder.cpp#L772) — Compteur de publications et échec observé.
- [src/core/cursiv/workspace/DefaultDocumentBuilder.hpp:196](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultDocumentBuilder.hpp#L196) — Filtrage des tentatives échouées.

**Comparaison.** Langium attend également un événement futur ou l’annulation de son propre token et ne fait pas de l’annulation d’un build une issue de tous les waiters. Cursiv ajoute BuildFailed et les compteurs de publication pour distinguer les erreurs ordinaires et les jalons transitoires.

**Preuves de comparaison.**

- [langium/packages/langium/src/workspace/document-builder.ts:586](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/workspace/document-builder.ts#L586) — Attente du jalon global et token propre.

**Limites.** Il s’agit d’une obligation d’intégration, pas d’un deadlock du builder ; les waits portent contractuellement sur un jalon et non une tentative.

<a id="comparison-wsp-029"></a>

### WSP-029 — Barrières entre scopes, linking et publication du contenu

**Question.** Comparer les garanties entre calcul des scopes et linking, en tenant compte du parallélisme natif de Cursiv.

**Cursiv.** Avant toute admission Immediate, buildDocuments délie les mémos non indexés et traite les dépendants affectés. Si de nouveaux exports peuvent modifier des documents déjà scopés, prepareContent diffère settle jusqu’à publication des exports et reset des nouveaux dépendants, même si la phase de contenu a partiellement échoué. Une phase séparée ComputedScopes est entièrement jointe avant le premier linking. bindReferences publie le linker en release après les localSymbols. Linking et indexation des références peuvent ensuite avancer par document ; un échec entre les deux ramène un état Linked à ComputedScopes pour reprise avant le prochain settle.

**Preuves Cursiv.**

- [src/core/cursiv/workspace/DefaultDocumentBuilder.cpp:293](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultDocumentBuilder.cpp#L293) — Collecte des dépendants et travail accru.
- [src/core/cursiv/workspace/DefaultDocumentBuilder.cpp:320](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultDocumentBuilder.cpp#L320) — Décision de différer settle.
- [src/core/cursiv/workspace/DefaultDocumentBuilder.cpp:347](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultDocumentBuilder.cpp#L347) — Invalidation après exports avant settle.
- [src/core/cursiv/workspace/DefaultDocumentBuilder.cpp:374](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultDocumentBuilder.cpp#L374) — Barrière complète scopes→linking.
- [src/core/cursiv/workspace/DefaultDocumentBuilder.cpp:398](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultDocumentBuilder.cpp#L398) — Reprise d’un linking non indexé.
- [src/core/cursiv/workspace/DefaultDocumentBuilder.cpp:650](https://github.com/ydaveluy/cursiv/blob/eabc0d70687bec94eb47e42e08b3987d4d3b8038/src/core/cursiv/workspace/DefaultDocumentBuilder.cpp#L650) — Publication des références après les scopes.

**Comparaison.** Les deux builders séparent contenu, scopes et linking. Cursiv doit en plus joindre les workers et différer settle lorsqu’une publication invalide des lecteurs ; ces barrières natives n’ont pas d’équivalent mécanique à recopier depuis la boucle async de Langium.

**Preuves de comparaison.**

- [langium/packages/langium/src/workspace/document-builder.ts:411](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/workspace/document-builder.ts#L411) — Ordre parse/index/scopes.
- [langium/packages/langium/src/workspace/document-builder.ts:425](https://github.com/eclipse-langium/langium/blob/89d49680b39218af2e5b700cd3f4fdc122600ac4/packages/langium/src/workspace/document-builder.ts#L425) — Linking puis indexation des références.

**Limites.** Les dépendances supplémentaires d’un ScopeProvider doivent être décrites par son contrat/isAffected. Une défaillance d’allocation pendant les invalidations n’est pas une transaction atomique globale.


## Couverture des 513 périmètres

Cette matrice donne le statut et les causes associées à chacun des petits périmètres. Les fiches sans constat n’appellent pas de correction présumée. Les détails des constats, réserves et comparaisons nécessaires aux corrections sont intégralement présents plus haut.

| ID | Périmètre | Verdict | Constats associés |
|---|---|---|---|
| API-001 | Sérialisation réversible d’un modèle AST | constat | [API-AST-CODEC](#finding-api-ast-codec) |
| API-002 | Intégration d’un AST synthétique dans un document | constat | [API-FROM-MODEL](#finding-api-from-model) |
| API-003 | Traçabilité du texte généré vers le modèle source | constat | [API-GENERATION-TRACE](#finding-api-generation-trace) |
| API-004 | Production de règles de coloration depuis la grammaire | constat | [API-HIGHLIGHT-GENERATION](#finding-api-highlight-generation) |
| API-005 | Cycle de vie d’un notebook et de ses cellules | constat | [API-NOTEBOOK-LIFECYCLE](#finding-api-notebook-lifecycle) |
| API-006 | Choix d’une règle d’entrée pour un fragment | constat | [API-ENTRY-RULE](#finding-api-entry-rule) |
| API-007 | Contrôle public de la récupération d’erreurs | constat | [API-RECOVERY-POLICY](#finding-api-recovery-policy) |
| ASS-001 | Marqueurs de texte et normalisation des positions attendues | constat | [ASS-MARQUEURS-MALFORMES](#finding-ass-marqueurs-malformes) |
| ASS-002 | État initial, nettoyage et réalisme des workspaces de test | sans constat établi |  |
| ASS-003 | Projection et comparaison des résultats de complétion et de symboles | constat | [ASS-LISTE-CARDINALITE-EFFACEE](#finding-ass-liste-cardinalite-effacee) |
| ASS-004 | Filtrage des diagnostics et sens des assertions positives/négatives | sans constat établi |  |
| ASS-005 | Attentes de formatting, renommage et actions de code | constat | [ASS-CODEACTION-URI-EFFACEE](#finding-ass-codeaction-uri-effacee) |
| ASS-006 | Oracle CST et rôle des empreintes de tape | constat | [ASS-CST-SOUSARBRE-TRAVERSE](#finding-ass-cst-sousarbre-traverse) |
| ASS-007 | Comparateur AST, lecteurs de valeurs et exclusions | constat | [ASS-AST-OPTIONAL-SENTINEL](#finding-ass-ast-optional-sentinel) |
| ASS-008 | Dérivation déclarative de documents et d’AST attendus | sans constat établi |  |
| ASS-009 | Choix pseudoaléatoires, rejouabilité et comptage des cas jugés | sans constat établi |  |
| ASS-010 | Génération de grammaires aléatoires et familles admissibles | sans constat établi |  |
| ASS-011 | Reconnaisseur de référence des grammaires aléatoires | constat | [ASS-RGRAM-CAMPAGNE-VIDE](#finding-ass-rgram-campagne-vide) |
| ASS-012 | Validité des jumeaux, mutations et cartographie des offsets | sans constat établi |  |
| ASS-013 | Appariement des fautes et conservation du texte/AST | sans constat établi |  |
| ASS-014 | Agrégation, signatures et régénération des résultats attendus | constat | [ASS-REPAIR-JSON-BUDGET](#finding-ass-repair-json-budget) |
| ASS-015 | Fenêtres de temps, phases et échauffement | sans constat établi |  |
| ASS-016 | Attribution et disponibilité des compteurs matériels | sans constat établi |  |
| ASS-017 | Contenu et représentativité des corpus de benchmark | sans constat établi |  |
| ASS-018 | Sondes de services et signification des distributions | sans constat établi |  |
| ASS-019 | Preuves de croissance des opérations et du coût CPU | constat | [ASS-CPU-IGNORE-SILENCIEUX](#finding-ass-cpu-ignore-silencieux) |
| ASS-020 | Sessions LSP/MCP : cadrage, corrélation et assertions d’ordre | constat | [ASS-DIAGNOSTIC-ANTERIEUR](#finding-ass-diagnostic-anterieur), [ASS-SESSION-ARRET-IGNORE](#finding-ass-session-arret-ignore) |
| ASS-021 | Attribution de la mémoire de pic et de la rétention | sans constat établi |  |
| ASS-022 | Transmission des assertions au verdict final | sans constat établi |  |
| ASS-023 | Lecture et disponibilité des indicateurs de mémoire résidente | constat | [ASS-RSS-ETAT-PIC](#finding-ass-rss-etat-pic) |
| AST-001 | Allocation des blocs alignés | constat | [CORE-BLOCK-ARITH](#finding-core-block-arith) |
| AST-002 | Construction et destruction dans l'arène AST | sans constat établi |  |
| AST-003 | Tags de type et conversions vers les bases | constat | [CORE-AST-COPY-TAG](#finding-core-ast-copy-tag) |
| AST-004 | Accès réflexif aux membres | sans constat établi |  |
| AST-005 | Réflexion et filtrage de types | sans constat établi |  |
| AST-006 | Parcours AST et appartenance au document | sans constat établi |  |
| AST-007 | Propriété d'un ParseResult et vues empruntées | sans constat établi |  |
| AST-008 | Réduction CST vers objets AST | sans constat établi |  |
| AST-009 | Écritures différées et listes AST | sans constat établi |  |
| AST-010 | Conversions scalaires et erreurs de conversion | constat | [CORE-CONVERSION-POLL](#finding-core-conversion-poll) |
| AST-011 | AST construit autour de réparations | sans constat établi |  |
| AST-012 | Compactage des nœuds et références conservées | sans constat établi |  |
| BLD-001 | Consommation du projet CMake | sans constat établi |  |
| BLD-002 | Acquisition de lsp-framework et génération de son protocole | sans constat établi |  |
| BLD-003 | Validation d’une déclaration de langage CMake | sans constat établi |  |
| BLD-004 | Compilation de l’exécutable générateur natif | constat | [BLD-IDENTITE-VIRGULE](#finding-bld-identite-virgule) |
| BLD-005 | Dépendances et unités du parser généré | sans constat établi |  |
| BLD-006 | Sous-build hôte en compilation croisée | sans constat établi |  |
| BLD-007 | Contrat de la toolchain WASI | sans constat établi |  |
| BLD-008 | Téléchargement et installation du SDK | sans constat établi |  |
| BLD-009 | Sélection et intégration de l’allocateur | sans constat établi |  |
| BLD-010 | Collecte des modules Wasm et notices | constat | [BLD-PACKAGE-RAFRAICHISSEMENT](#finding-bld-package-rafraichissement), [BLD-DISTRIBUTION-ANCIENS-MODULES](#finding-bld-distribution-anciens-modules) |
| BLD-011 | Assemblage de la démo web | sans constat établi |  |
| BLD-012 | Bundle et packaging des extensions VSIX | sans constat établi |  |
| BLD-013 | Chaîne F5 et preset actif | sans constat établi |  |
| BLD-014 | Production et paramétrage du scaffolder | constat | [BLD-SCAFFOLD-EXT-NONBORNEE](#finding-bld-scaffold-ext-nonbornee) |
| BLD-015 | Composition du projet généré | sans constat établi |  |
| BLD-016 | Matrice CI et identité des artefacts vérifiés | sans constat établi |  |
| BLD-017 | Publication du site et notifications après CI | sans constat établi |  |
| BLD-018 | Périmètre de l’analyse CodeQL | sans constat établi |  |
| BLD-019 | Production et export de couverture | réserve | [Réserve](#reserve-bld-019) |
| BLD-020 | Mise à jour automatisée des dépendances | constat | [BLD-DEPENDANCES-NPM-HORS-SUIVI](#finding-bld-dependances-npm-hors-suivi) |
| BLD-023 | Lecture des tailles de frames Wasm | constat | [BLD-FRAMES-ARGUMENTS-INVALIDES](#finding-bld-frames-arguments-invalides) |
| BLD-024 | Déclaration CMake d’un benchmark consommateur | sans constat établi |  |
| CAC-001 | Lecture d’une table concurrente | constat | [WS-CAC-FUNCTOR-CONTRACT](#finding-ws-cac-functor-contract) |
| CAC-002 | Parcours avec callback d’une table | sans constat établi |  |
| CAC-003 | Réservation et publication d’une entrée | sans constat établi |  |
| CAC-004 | Migration coopérative d’une table | sans constat établi |  |
| CAC-005 | Effacement et générations de table | sans constat établi |  |
| CAC-006 | Enregistrement des lecteurs par epoch | constat | [WS-CAC-SLOT-EXHAUSTION](#finding-ws-cac-slot-exhaustion), [WS-CAC-BOOTSTRAP-NOEXCEPT](#finding-ws-cac-bootstrap-noexcept) |
| CAC-007 | Transfert d’ownership à la file des retraits | constat | [WS-CAC-RETIRE-OWNERSHIP](#finding-ws-cac-retire-ownership) |
| CAC-008 | Collecte globale des tables retirées | constat | [WS-CAC-DEFERRED-PAYLOAD](#finding-ws-cac-deferred-payload) |
| CAC-009 | Handle de lecture d’un snapshot | sans constat établi |  |
| CAC-010 | Publication et retrait des snapshots | constat | [WS-CAC-SNAPSHOT-RETENTION](#finding-ws-cac-snapshot-retention) |
| CAC-011 | Factory de mémoïsation partagée | sans constat établi |  |
| CAC-012 | Partitionnement documentaire et accès TLS | constat | [WS-CAC-DOCUMENT-PARTITIONS](#finding-ws-cac-document-partitions) |
| CAC-013 | Invalidation et abonnement du cache documentaire | sans constat établi |  |
| CAC-014 | Invalidation du cache workspace | sans constat établi |  |
| CGR-001 | Portée des règles examinées pour les avertissements | sans constat établi |  |
| CGR-002 | Absence de lecture dans la grammaire écrite | sans constat établi |  |
| CGR-003 | Reconnaissance d’une répétition dans une lecture écrite | sans constat établi |  |
| CGR-004 | Expansion des appels non cycliques de TwinReader | sans constat établi |  |
| CGR-005 | Identité canonique des formes lues | sans constat établi |  |
| CGR-006 | Suppression d’une branche couverte par des branches antérieures | sans constat établi |  |
| CGR-007 | Réécriture d’une suite d’éléments optionnels équivalents | constat | [CGR-OPTION-CROISSANCE](#finding-cgr-option-croissance) |
| CGR-008 | Énumération bornée des spellings d’opérateurs | constat | [CGR-SPELL-ALIAS](#finding-cgr-spell-alias) |
| CGR-009 | Décision de conflit entre niveaux unaires | constat | [CGR-UNARY-LIMITE](#finding-cgr-unary-limite) |
| CGR-010 | Repérage des alternatives bornées par leur propre règle | sans constat établi |  |
| CGR-011 | Signalement d’une répétition portant sur un run | sans constat établi |  |
| CGR-012 | Signalement d’un littéral de mot sans garde | sans constat établi |  |
| CGR-013 | Repérage des lectures impossibles après EOF | sans constat établi |  |
| CGR-014 | Classification des répétitions non bornées dans les conditions | constat | [CGR-WARN-IDENTITE](#finding-cgr-warn-identite) |
| CLI-001 | Arguments et aide de la CLI | sans constat établi |  |
| CLI-002 | Amorçage du workspace CLI | sans constat établi |  |
| CLI-003 | Chargement et build du fichier demandé | sans constat établi |  |
| CLI-004 | Diagnostics, flux et code de sortie | sans constat établi |  |
| CLI-005 | Dump textuel et JSON du CST | constat | [CLI-DUMP-UTF8-COUPE](#finding-cli-dump-utf8-coupe) |
| COM-001 | Échelle de prix des gestes et des fautes de frappe | sans constat établi |  |
| COM-002 | Conversion de la configuration en quota de travail | sans constat établi |  |
| COM-003 | Reprise d’une recherche dans le quota du parse | sans constat établi |  |
| COM-004 | Ordre stable de la queue de réparation | sans constat établi |  |
| COM-005 | Admission d’un reading par dominance commune | sans constat établi |  |
| COM-006 | Élection commune entre groupes de buts | constat | [COM-ELECTION-HORS-QUOTA](#finding-com-election-hors-quota) |
| COM-007 | Divergence et préférence des histoires de gestes | sans constat établi |  |
| COM-008 | Conservation des insertions alternatives après fusion | sans constat établi |  |
| COM-009 | Énumération progressive des fins de relecture | sans constat établi |  |
| COM-010 | Projection d’une faute close en note commune | sans constat établi |  |
| COM-011 | Exploration des fautes de frappe dans le DFA | réserve | [Réserve](#reserve-com-011) |
| COM-012 | Bornes du parcours typo et statut incomplet | constat | [COM-TYPO-INCOMPLET](#finding-com-typo-incomplet) |
| COM-013 | Admission d’un terminal relu à sa frontière propre | sans constat établi |  |
| COM-014 | Identité et ordre des candidats typo | sans constat établi |  |
| COM-015 | Table de clés entières à insertion seule | sans constat établi |  |
| COM-016 | Unicité d’une marque par tour | sans constat établi |  |
| COM-017 | Transition d’un petit stockage inline vers le tas | sans constat établi |  |
| COM-018 | Représentation canonique d’un ensemble dynamique | constat | [COM-DYNSET-COUT-CREUX](#finding-com-dynset-cout-creux) |
| COM-019 | Internement d’un lot d’ensembles | sans constat établi |  |
| COM-020 | Index de collisions sur des identifiants externes | sans constat établi |  |
| COM-021 | Identité et bornes d’une chaîne d’actions internée | constat | [COM-CHAINS-SEUIL-DECALAGE](#finding-com-chains-seuil-decalage) |
| COM-022 | Composition de chaînes et de leur documentation | sans constat établi |  |
| COM-023 | Qualification de construction d’une chaîne | sans constat établi |  |
| COM-024 | Observation périodique d’un token d’annulation | sans constat établi |  |
| COM-025 | Portée dynamique du token sur un thread | sans constat établi |  |
| CST-001 | Croissance et invalidation des vues du flux CST | constat | [CORE-BLOCK-ARITH](#finding-core-block-arith) |
| CST-002 | Ouverture et fermeture d'un sous-arbre CST | sans constat établi |  |
| CST-003 | Feuilles, trivia et records de réparation | sans constat établi |  |
| CST-004 | Index paresseux des feuilles | sans constat établi |  |
| CST-005 | Navigation parents, enfants et voisinage | constat | [LSP-FORMAT-KEYWORD-CASSE](#finding-lsp-format-keyword-casse) |
| CST-006 | Liaison du CST à sa grammaire et son modèle | sans constat établi |  |
| CST-007 | Projection des positions après traduction source | constat | [CORE-TRANSLATION-ROUNDTRIP](#finding-core-translation-roundtrip) |
| EXA-001 | Contrat syntaxique des expressions et des appels | sans constat établi |  |
| EXA-002 | Évaluation numérique, liaison des arguments et fin d’évaluation | constat | [EXA-EVALUATION-SILENCIEUSE](#finding-exa-evaluation-silencieuse), [EXA-ARITH-PROFONDEUR](#finding-exa-arith-profondeur), [EXA-PARAMETRE-CALLABLE](#finding-exa-parametre-callable), [EXA-CLI-EXCEPTIONS](#finding-exa-cli-exceptions) |
| EXA-003 | Unicité des noms de définitions et de paramètres | sans constat établi |  |
| EXA-004 | Construction du graphe d’appels et signalement de la récursion | constat | [EXA-CYCLES-ARGUMENTS](#finding-exa-cycles-arguments), [EXA-ARITH-PROFONDEUR](#finding-exa-arith-profondeur) |
| EXA-005 | Recherche des divisions et modulos par zéro sous budget | constat | [EXA-ARITH-PROFONDEUR](#finding-exa-arith-profondeur) |
| EXA-006 | Normalisation constante jusqu’à l’édition de remplacement | constat | [EXA-NORMALISATION-SYNTAXE](#finding-exa-normalisation-syntaxe), [EXA-ARITH-PROFONDEUR](#finding-exa-arith-profondeur) |
| EXA-007 | Mise en forme des expressions, appels et commentaires | constat | [LSP-FORMAT-KEYWORD-CASSE](#finding-lsp-format-keyword-casse), [LSP-FORMAT-COMMENT-IDEMPOTENCE](#finding-lsp-format-comment-idempotence) |
| EXA-008 | Contrat des packages, types et propriétés du modèle | sans constat établi |  |
| EXA-009 | Qualification et visibilité des types dans les packages | constat | [EXA-DOMAIN-PROFONDEUR](#finding-exa-domain-profondeur) |
| EXA-010 | Détection des cycles d’héritage entre documents | constat | [EXA-HERITAGE-REVALIDATION](#finding-exa-heritage-revalidation) |
| EXA-011 | Renommage des packages et propagation des noms qualifiés | constat | [EXA-DOMAIN-PROFONDEUR](#finding-exa-domain-profondeur) |
| EXA-012 | Projection des entités en classes et types Java | constat | [EXA-JAVA-IDENTITES](#finding-exa-java-identites), [EXA-JAVA-NOMS](#finding-exa-java-noms) |
| EXA-013 | Correspondance packages Java et arborescence des fichiers | constat | [EXA-GENERATION-ECRITURE](#finding-exa-generation-ecriture), [EXA-CLI-EXCEPTIONS](#finding-exa-cli-exceptions), [EXA-JAVA-PACKAGE-CHEMIN](#finding-exa-java-package-chemin), [EXA-JAVA-IDENTITES](#finding-exa-java-identites), [EXA-DOMAIN-PROFONDEUR](#finding-exa-domain-profondeur) |
| EXA-014 | Mise en forme des blocs et commentaires de documentation | constat | [LSP-FORMAT-KEYWORD-CASSE](#finding-lsp-format-keyword-casse), [LSP-FORMAT-COMMENT-IDEMPOTENCE](#finding-lsp-format-comment-idempotence) |
| EXA-015 | Contrat commun des langages d’exigences et de tests | constat | [EXA-REQ-CHAINES-LANGIUM](#finding-exa-req-chaines-langium) |
| EXA-016 | Convention d’identification des exigences et des tests | sans constat établi |  |
| EXA-017 | Couverture inverse d’une exigence par les tests du workspace | constat | [EXA-REQ-COUVERTURE-PERIMEE](#finding-exa-req-couverture-perimee) |
| EXA-018 | Compatibilité des environnements d’un test avec ses exigences | sans constat établi |  |
| EXA-019 | Périmètre et contenu du rapport HTML de couverture | constat | [EXA-REQ-RAPPORT-ERREURS](#finding-exa-req-rapport-erreurs), [EXA-REQ-RAPPORT-COUT](#finding-exa-req-rapport-cout) |
| EXA-020 | Mise en forme coordonnée des deux modèles | constat | [LSP-FORMAT-KEYWORD-CASSE](#finding-lsp-format-keyword-casse) |
| EXA-021 | Contrat syntaxique des machines et références typées | constat | [EXA-SM-REFERENCES-EXTERNES](#finding-exa-sm-references-externes) |
| EXA-022 | Identité et admissibilité des noms de machine | constat | [EXA-SM-NOMS-CPP](#finding-exa-sm-noms-cpp) |
| EXA-023 | Sélection d’une transition par événement et génération des handlers | constat | [EXA-SM-TRANSITIONS-COUT](#finding-exa-sm-transitions-cout), [EXA-SM-REFERENCES-EXTERNES](#finding-exa-sm-references-externes) |
| EXA-024 | Durée de vie des états et contexte de la machine générée | constat | [EXA-SM-COPIE-OWNERSHIP](#finding-exa-sm-copie-ownership) |
| EXA-025 | Sémantique des commandes et actions associées aux états | constat | [EXA-SM-ACTIONS-OMISES](#finding-exa-sm-actions-omises) |
| EXA-026 | Programme autonome généré et contrat d’entrée-sortie | constat | [EXA-SM-REFERENCES-EXTERNES](#finding-exa-sm-references-externes) |
| EXA-027 | Mise en forme des sections, états et actions | constat | [LSP-FORMAT-KEYWORD-CASSE](#finding-lsp-format-keyword-casse) |
| EXA-028 | Arité des appels et nature de leur cible | constat | [EXA-PARAMETRE-CALLABLE](#finding-exa-parametre-callable) |
| EXA-029 | Convention de capitale initiale des noms de types | sans constat établi |  |
| EXA-030 | Choix et publication du fichier C++ généré | constat | [EXA-GENERATION-ECRITURE](#finding-exa-generation-ecriture), [EXA-CLI-EXCEPTIONS](#finding-exa-cli-exceptions) |
| EXA-031 | Choix et publication du rapport HTML | constat | [EXA-GENERATION-ECRITURE](#finding-exa-generation-ecriture), [EXA-CLI-EXCEPTIONS](#finding-exa-cli-exceptions), [EXA-REQ-RAPPORT-ERREURS](#finding-exa-req-rapport-erreurs) |
| GRA-001 | Normalisation des expressions du DSL | sans constat établi |  |
| GRA-002 | Identité des règles et composition de grammaires | constat | [CORE-GRAMMAR-OVERRIDE-DIAMOND](#finding-core-grammar-override-diamond) |
| GRA-003 | Directives d'affectation et association AST | sans constat établi |  |
| GRA-004 | Descripteurs C++ vers image de grammaire | réserve | [Réserve](#reserve-gra-004) |
| GRA-005 | Réécritures communes et commits | sans constat établi |  |
| GRA-006 | Descriptions et paramètres communs du lowering | constat | [LR-OPTIONAL-GATE-INTERNE](#finding-lr-optional-gate-interne) |
| GRA-007 | Entrées et introspection de la grammaire | constat | [CORE-GRAMMAR-RUNTIME-MODEL](#finding-core-grammar-runtime-model) |
| GRA-008 | Texte source et dump de grammaire | sans constat établi |  |
| GRA-009 | Registre d'actions AST générées | sans constat établi |  |
| GRA-010 | Métadonnées AST émises | sans constat établi |  |
| GRA-011 | Encodage commun des records et stamps | sans constat établi |  |
| GRA-012 | Publication des fichiers générés | constat | [CORE-GEN-PARTIAL-PUBLISH](#finding-core-gen-partial-publish) |
| GRA-013 | Diagnostics de grammaire | sans constat établi |  |
| GRA-014 | Encodage des octets dans les littéraux C++ | sans constat établi |  |
| HOST-001 | Amorçage du runner Node | sans constat établi |  |
| HOST-002 | Décodage de la mémoire importée Wasm | constat | [HOST-MEMORY-READER-DRIFT](#finding-host-memory-reader-drift) |
| HOST-003 | Correspondance des chemins hôte/invité | constat | [HOST-WINDOWS-NONEXISTENT-PATH](#finding-host-windows-nonexistent-path) |
| HOST-004 | Adaptation de fd_readdir sur Node Windows | sans constat établi |  |
| HOST-005 | Extension applicative du runner WASI | sans constat établi |  |
| HOST-006 | Threads WASI et sortie du processus Node | réserve | [Réserve](#reserve-host-006) |
| HOST-007 | Filesystem virtuel de la démo | constat | [HOST-WEB-FD-THREAD-LOCAL](#finding-host-web-fd-thread-local) |
| HOST-008 | ABI des imports WASI auxiliaires du navigateur | constat | [HOST-WEB-POLL-DEADLINES](#finding-host-web-poll-deadlines) |
| HOST-009 | Anneau partagé pour stdin dans la démo | constat | [HOST-WEB-RING-SIGNED](#finding-host-web-ring-signed) |
| HOST-010 | Sortie Wasm et cadrage LSP dans la page | constat | [HOST-WEB-FRAME-FAILURE](#finding-host-web-frame-failure) |
| HOST-011 | Slots de threads de la démo | constat | [HOST-WEB-WORKER-LOST-WAKE](#finding-host-web-worker-lost-wake) |
| HOST-012 | Session JSON-RPC du client web | sans constat établi |  |
| HOST-013 | Changement et redémarrage du langage de démo | constat | [HOST-WEB-SESSION-ERROR](#finding-host-web-session-error) |
| HOST-014 | Synchronisation des modèles et diagnostics de démo | sans constat établi |  |
| HOST-015 | Adaptation de la complétion vers Monaco | constat | [HOST-WEB-COMPLETION-PARTIAL](#finding-host-web-completion-partial) |
| HOST-016 | Conversion des résultats de lecture Monaco | constat | [HOST-WEB-PLAINTEXT-MARKDOWN](#finding-host-web-plaintext-markdown) |
| HOST-017 | Conversion des modifications LSP vers Monaco | constat | [HOST-WEB-EDIT-VERSION](#finding-host-web-edit-version) |
| HOST-018 | Légende et rafraîchissement des semantic tokens web | sans constat établi |  |
| HOST-019 | Résolution de la commande serveur VS Code | constat | [HOST-EXTENSION-DUPLICATION](#finding-host-extension-duplication) |
| HOST-020 | Activation et arrêt des clients VS Code | sans constat établi |  |
| HOST-021 | Admission HTTP du bridge MCP d’extension | sans constat établi |  |
| HOST-022 | Transmission et fermeture du bridge MCP | constat | [HOST-MCP-LISTEN-ERROR](#finding-host-mcp-listen-error) |
| LEX-001 | Alphabet, classes Unicode et frontières de mot | sans constat établi |  |
| LEX-002 | Réification des terminaux et résolution des alias | sans constat établi |  |
| LEX-003 | Admissibilité et réécriture de la récursion lexicale | sans constat établi |  |
| LEX-004 | Reconnaissance des formes imbriquées, marques et échos | sans constat établi |  |
| LEX-005 | Construction du NFA lexical | sans constat établi |  |
| LEX-006 | Déterminisation et identité des lectures DFA | sans constat établi |  |
| LEX-007 | Minimisation et remplissage des tables lexicales | sans constat établi |  |
| LEX-008 | Spécialisation des mots-clés et coupures | sans constat établi |  |
| LEX-009 | Produits de refus et reconnaissance incomplète | réserve | [Réserve](#reserve-lex-009) |
| LEX-010 | Couverture des modes et analyses de concurrence lexicale | sans constat établi |  |
| LEX-011 | Parité des scans génériques et chemins rapides générés | sans constat établi |  |
| LEX-012 | Scan, plus longue lecture et élection du token | sans constat établi |  |
| LEX-013 | Frontières de mots, réservations et refus | sans constat établi |  |
| LEX-014 | Lancement, jonction et reprise des relais | sans constat établi |  |
| LEX-015 | Recherche de fermetures et reprise des marques | sans constat établi |  |
| LEX-016 | Session de suffixes et cache des marches échouées | sans constat établi |  |
| LEX-017 | Machine lexicale, environnements de captures et retour | sans constat établi |  |
| LEX-018 | Compilation des prédicats lexicaux et reconnaisseurs | sans constat établi |  |
| LEX-019 | Compilation des conditions sur suites de tokens | sans constat établi |  |
| LEX-020 | Ensembles de curseurs, répétitions et résultats mémorisés | constat | [LEX-FRONT-INSERTIONS](#finding-lex-front-insertions) |
| LEX-021 | Classification et émission des expressions de gap | sans constat établi |  |
| LEX-022 | Reconnaissance des gaps et ensembles de positions | sans constat établi |  |
| LEX-023 | Comparaison physique des marges et identité des niveaux | sans constat établi |  |
| LEX-024 | Tokens virtuels, delimiters et transitions de niveau | sans constat établi |  |
| LEX-025 | Budget de lecture, annulation et statuts incomplets | constat | [LEX-ANNULATION-TOKEN](#finding-lex-annulation-token) |
| LL-001 | Abaissement des règles, enveloppes et fins explicites | sans constat établi |  |
| LL-002 | Abaissement des répétitions et de leurs bornes | sans constat établi |  |
| LL-003 | Placement des actions et propriété des records | sans constat établi |  |
| LL-004 | Reconnaissance et repli des boucles gauches | sans constat établi |  |
| LL-005 | Abaissement des opérateurs et niveaux de précédence | constat | [LL-OP-ALIAS-CROISSANCE](#finding-ll-op-alias-croissance) |
| LL-006 | Fixpoints de nullabilité physique et logique | sans constat établi |  |
| LL-007 | Propagation de FOLLOW et des continuations infix | sans constat établi |  |
| LL-008 | Atteignabilité, productivité et refus de grammaire | constat | [LL-PRODUCTIVITE-EOF-INDIRECT](#finding-ll-productivite-eof-indirect) |
| LL-009 | Construction de l’ATN et identité des sites | sans constat établi |  |
| LL-010 | Classes de retour et états après une position | constat | [LL-RETURN-SUFFIX-RESCAN](#finding-ll-return-suffix-rescan) |
| LL-011 | Classification des décisions et construction du DFA de regard | constat | [LL-DFA-CAP-DEPASSEMENT](#finding-ll-dfa-cap-depassement) |
| LL-012 | Assemblage des modes lexicaux demandés par LL | sans constat établi |  |
| LL-013 | Frames, appels suspendus et profondeur plain | sans constat établi |  |
| LL-014 | Émission des choix, séquences et répétitions | sans constat établi |  |
| LL-015 | Émission du calcul infix et des fusions associées | sans constat établi |  |
| LL-016 | Encodage et assemblage des tables générées | sans constat établi |  |
| LL-017 | Stamps et correspondance records–règles–features | sans constat établi |  |
| LL-018 | Finalisation du programme LL et options exposées | sans constat établi |  |
| LL-019 | Cycle de vie de l’API Parser LL | sans constat établi |  |
| LL-020 | Stockage des frames et reprise de la machine LL | sans constat établi |  |
| LL-021 | Lecture détenue et changement de mode | sans constat établi |  |
| LL-022 | Raccord des chemins single, staged et adaptive | sans constat établi |  |
| LL-023 | Évaluation des conditions et tokens implicites LL | sans constat établi |  |
| LL-024 | Adaptation LL de l’indentation | sans constat établi |  |
| LL-025 | Consommation, trivia et fusions de CST | sans constat établi |  |
| LL-026 | Sessions, réentrance et ressources TLS | constat | [LL-EPISODE-TLS-RETENTION](#finding-ll-episode-tls-retention) |
| LL-027 | Conversion et navigation des contextes exacts/prêtés | sans constat établi |  |
| LL-028 | Interning, unions et scratch du DAG de contextes | sans constat établi |  |
| LL-029 | Historiques persistants et relation de préférence | réserve | [Réserve](#reserve-ll-029) |
| LL-030 | Cellules de piles et partage d’appels | constat | [LL-CELLULE-APPELS-SCAN](#finding-ll-cellule-appels-scan) |
| LL-031 | Clôture et filtrage du simulateur d’épisode | sans constat établi |  |
| LL-032 | Gestion et fusion du front de positions d’épisode | constat | [LL-FRONT-DEPLACEMENTS](#finding-ll-front-deplacements) |
| LL-033 | Clés, égalité et rétention du mémo d’épisode | constat | [LL-MEMO-VOLUME](#finding-ll-memo-volume) |
| LL-034 | Installation, reprise et invalidation des plans | sans constat établi |  |
| LL-035 | Clôtures partagées, fallback et cache de transitions | sans constat établi |  |
| LL-036 | Identité d’un futur et admission des candidats | sans constat établi |  |
| LL-037 | Production des insertions, remplacements et suppressions | sans constat établi |  |
| LL-038 | Réinterprétation lexicale et fautes de mot | constat | [COM-TYPO-INCOMPLET](#finding-com-typo-incomplet) |
| LL-039 | Validation, course et choix d’une réparation | sans constat établi |  |
| LL-040 | Troncature des appelants et abandon de frames | sans constat établi |  |
| LL-041 | Budget externe et propagation de l’annulation | constat | [LL-SIMULATION-BUDGET-DIFFERE](#finding-ll-simulation-budget-differe) |
| LL-042 | Application des gestes et regroupement des diagnostics | sans constat établi |  |
| LL-043 | Repli borné, fins d’entrée et sorties de boucle | sans constat établi |  |
| LL-044 | Reconstruction du contexte depuis le CST | sans constat établi |  |
| LL-045 | Replay progressif et filtrage du préfixe | sans constat établi |  |
| LL-046 | Projection multibranche vers les features de complétion | constat | [CORE-COMPLETION-BOUNDS](#finding-core-completion-bounds) |
| LL-047 | Unités des compteurs de génération et de prédiction | sans constat établi |  |
| LL-048 | Expansion des groupes non ordonnés | sans constat établi |  |
| LL-049 | Propagation des ensembles FIRST | sans constat établi |  |
| LL-050 | Pile logique des retours de site et lecture des appelants | sans constat établi |  |
| LL-051 | Ouverture, fin et restitution des sections LL | sans constat établi |  |
| LL-052 | Arbitrage lexical et préférence du candidat d’épisode | sans constat établi |  |
| LL-053 | Assemblage des artefacts LL avant écriture | sans constat établi |  |
| LR-001 | Entrées et cycle de vie du générateur | sans constat établi |  |
| LR-002 | Déclaration des règles et contextes de lowering | sans constat établi |  |
| LR-003 | Application des directives aux formes intermédiaires | constat | [LR-LOWERING-PROFONDEUR](#finding-lr-lowering-profondeur) |
| LR-004 | Répétitions et positions optionnelles | constat | [LR-OPTIONAL-GATE-INTERNE](#finding-lr-optional-gate-interne) |
| LR-005 | Groupes non ordonnés | sans constat établi |  |
| LR-006 | Identité des productions et compaction du CFG | sans constat établi |  |
| LR-007 | Admission du CFG par productivité et cycles | sans constat établi |  |
| LR-008 | Propagation des records AST dans le CFG | sans constat établi |  |
| LR-009 | Opérateurs infixes et alias lexicaux | sans constat établi |  |
| LR-010 | Fermetures FIRST et LAST | sans constat établi |  |
| LR-011 | Kernels et graphe d'états LR | constat | [LR-POOLS-CROISSANCE-16](#finding-lr-pools-croissance-16) |
| LR-012 | Conditions et programmes de goto gardés | sans constat établi |  |
| LR-013 | Propagation des lookaheads et lookbacks | sans constat établi |  |
| LR-014 | Refusion du graphe large | sans constat établi |  |
| LR-015 | Contextes et scores de l'ordre écrit | sans constat établi |  |
| LR-016 | Formes persistantes de l'ordre écrit | constat | [LR-WRITTEN-CACHE-PARTIEL](#finding-lr-written-cache-partiel) |
| LR-017 | Rencontres et comparaison des dérivations | sans constat établi |  |
| LR-018 | Arbitrage des cellules et diagnostics d'ambiguïté | sans constat établi |  |
| LR-019 | Ordre des insertions proposées | sans constat établi |  |
| LR-020 | Folding des écritures et plans de réduction | sans constat établi |  |
| LR-021 | Élimination des productions unitaires | sans constat établi |  |
| LR-022 | Minimisation et transfert des colonnes d'état | sans constat établi |  |
| LR-023 | Configurations lexicales et ensembles de modes | sans constat établi |  |
| LR-024 | Automates lexicaux et multiplicité des candidats | sans constat établi |  |
| LR-025 | Peeks et subsomption des modes | sans constat établi |  |
| LR-026 | Résumés d’états pour clôture et validation des réparations | sans constat établi |  |
| LR-027 | Projection des sites vers les attentes et records | sans constat établi |  |
| LR-028 | Encodage des mots d'élection et renommages | sans constat établi |  |
| LR-029 | Compression des matrices de programmes | sans constat établi |  |
| LR-030 | Disposition et encodage des actions et gotos | sans constat établi |  |
| LR-031 | Dimensions et stockage des tables gelées | sans constat établi |  |
| LR-032 | Représentation compacte du CFG et des faits scalaires | sans constat établi |  |
| LR-033 | Walkers et sélection lexicale générés | sans constat établi |  |
| LR-034 | Prédicats et tokens implicites générés | sans constat établi |  |
| LR-035 | Dispatch, shifts et réductions générés | sans constat établi |  |
| LR-036 | Fragments, groupes et unités de compilation | constat | [LR-GROUP-LINES-ZERO](#finding-lr-group-lines-zero), [LR-GROUP-FRONT-RESCANS](#finding-lr-group-front-rescans) |
| LR-037 | Cycle d'un parse et publication du résultat | sans constat établi |  |
| LR-038 | Pile contiguë et vues de pile | sans constat établi |  |
| LR-039 | Réduction et écriture du CST | sans constat établi |  |
| LR-040 | Élection lexicale et refus contextuel | sans constat établi |  |
| LR-041 | Mémos des marches lexicales et des gaps | sans constat établi |  |
| LR-042 | Interpréteur commun des lectures et tokens virtuels | sans constat établi |  |
| LR-043 | Résolution spéculative d'une cellule | constat | [LR-SPECULATION-QUOTAS-PARTIELS](#finding-lr-speculation-quotas-partiels) |
| LR-044 | Traces d'élection et preuves exactes | constat | [LR-SPECULATION-QUOTAS-PARTIELS](#finding-lr-speculation-quotas-partiels) |
| LR-045 | Arènes et index du GSS | constat | [LR-RUNTIME-IDS32-SANS-GARDE](#finding-lr-runtime-ids32-sans-garde) |
| LR-046 | Séquences persistantes et résumés de forks | constat | [LR-RUNTIME-IDS32-SANS-GARDE](#finding-lr-runtime-ids32-sans-garde) |
| LR-047 | Construction et élection de la forêt | constat | [LR-GSS-ACCEPT-CHEMINS](#finding-lr-gss-accept-chemins), [LR-RUNTIME-IDS32-SANS-GARDE](#finding-lr-runtime-ids32-sans-garde) |
| LR-048 | Identité et progression du graphe de piles | sans constat établi |  |
| LR-049 | Pruning et ordre des piles rivales | sans constat établi |  |
| LR-050 | Acceptation et retour à une pile linéaire | constat | [LR-GSS-ACCEPT-CHEMINS](#finding-lr-gss-accept-chemins) |
| LR-051 | Sélection des sources de réparation | constat | [LR-GSS-SOURCES-CAP-LOCAL](#finding-lr-gss-sources-cap-local), [LR-GSS-READ-PAST-GLOBAL](#finding-lr-gss-read-past-global) |
| LR-052 | Boucle d'épisode et replay des actions | constat | [LR-GSS-ELECTION-SHIFTS-QUADRATIQUE](#finding-lr-gss-election-shifts-quadratique) |
| LR-053 | Piles cactus et retour arrière d'une recherche | constat | [LR-RUNTIME-IDS32-SANS-GARDE](#finding-lr-runtime-ids32-sans-garde) |
| LR-054 | Identité des futurs et phases des candidats | réserve | [Réserve](#reserve-lr-054) |
| LR-055 | Générateurs paresseux de gestes | sans constat établi |  |
| LR-056 | Relecture lexicale et caches du réparateur | constat | [COM-TYPO-INCOMPLET](#finding-com-typo-incomplet) |
| LR-057 | Borne inférieure par programmation dynamique | constat | [LR-DP-TRAVAIL-NON-FACTURE](#finding-lr-dp-travail-non-facture), [COM-TYPO-INCOMPLET](#finding-com-typo-incomplet) |
| LR-058 | Recherche best-first et allocation du travail | constat | [LR-DP-TRAVAIL-NON-FACTURE](#finding-lr-dp-travail-non-facture), [COM-ELECTION-HORS-QUOTA](#finding-com-election-hors-quota) |
| LR-059 | Regroupement et course entre buts | constat | [COM-ELECTION-HORS-QUOTA](#finding-com-election-hors-quota) |
| LR-060 | Planification et écriture des réparations | sans constat établi |  |
| LR-061 | Clôture dégradée et terminaison à EOF | sans constat établi |  |
| LR-062 | Frames et fermeture des piles au caret | constat | [CORE-COMPLETION-BOUNDS](#finding-core-completion-bounds), [LR-RUNTIME-IDS32-SANS-GARDE](#finding-lr-runtime-ids32-sans-garde) |
| LR-063 | Hypothèses de conditions et chaînes de passes | constat | [LR-CARET-HYPOTHESES-COMBINATOIRES](#finding-lr-caret-hypotheses-combinatoires) |
| LR-064 | Lecture lexicale autour du caret | constat | [LR-CARET-CACHES-LINEAIRES](#finding-lr-caret-caches-lineaires) |
| LR-065 | Rejeu du CST vers le caret | constat | [CORE-TRANSLATION-ROUNDTRIP](#finding-core-translation-roundtrip), [CORE-COMPLETION-BOUNDS](#finding-core-completion-bounds) |
| LR-066 | Essai de candidats et projection des features | constat | [LR-CARET-ECHANTILLONS-INCOMPLETS](#finding-lr-caret-echantillons-incomplets), [LR-CARET-CACHES-LINEAIRES](#finding-lr-caret-caches-lineaires), [CORE-COMPLETION-BOUNDS](#finding-core-completion-bounds) |
| LR-067 | Cache partagé des mots d'échantillonnage | constat | [LR-CARET-CACHE-DENSE](#finding-lr-caret-cache-dense) |
| LR-068 | Classes de futurs définies par les gotos et conditions | sans constat établi |  |
| LR-069 | Opérations sur bitsets statiques et stockage des ensembles | sans constat établi |  |
| LR-070 | Tables lexicales dérivées pour guider la réparation | sans constat établi |  |
| LR-071 | Déploiement des boucles récursives selon leur tête d’entrée | sans constat établi |  |
| LSP-001 | Écriture des trames et texte UTF-8 | sans constat établi |  |
| LSP-002 | Démarrage et arrêt de session | constat | [WS-LSP-SESSION-REUSE](#finding-ws-lsp-session-reuse) |
| LSP-003 | Admission des notifications | sans constat établi |  |
| LSP-004 | Barrière des notifications servies | constat | [WS-LSP-NOTIFICATION-BARRIER](#finding-ws-lsp-notification-barrier), [WS-LSP-NOTIFICATION-ERROR-SINK](#finding-ws-lsp-notification-error-sink) |
| LSP-005 | Annulation et erreurs de requête | constat | [WS-LSP-INLINE-CANCELLATION](#finding-ws-lsp-inline-cancellation) |
| LSP-006 | Pool de réponses et requêtes en vol | constat | [WS-LSP-PENDING-SUBMIT-EXCEPTION](#finding-ws-lsp-pending-submit-exception) |
| LSP-007 | Handlers personnalisés | constat | [WS-LSP-INITIALIZE-FLAG](#finding-ws-lsp-initialize-flag) |
| LSP-008 | Adaptation des résultats aux capacités client | sans constat établi |  |
| LSP-009 | Requêtes sortantes et callbacks client | constat | [WS-LSP-CLIENT-RESULT-ONLY](#finding-ws-lsp-client-result-only) |
| LSP-010 | Services installés et capacités annoncées | sans constat établi |  |
| LSP-011 | Ouverture d’un document | sans constat établi |  |
| LSP-012 | Modifications et versions | sans constat établi |  |
| LSP-013 | Fermeture et retour au contenu persistant | sans constat établi |  |
| LSP-014 | Événements de fichiers surveillés | sans constat établi |  |
| LSP-015 | Dossiers de workspace | constat | [WS-LSP-WORKSPACE-FOLDER-CHANGES](#finding-ws-lsp-workspace-folder-changes) |
| LSP-016 | Acquisition et cache de configuration | sans constat établi |  |
| LSP-017 | Configuration et catégories de validation | sans constat établi |  |
| LSP-018 | Hooks d’opérations sur fichiers | sans constat établi |  |
| LSP-019 | Prérequis de phase et langue du document | constat | [WS-LSP-SERVICE-REQUIREMENT-EXTENSION](#finding-ws-lsp-service-requirement-extension) |
| LSP-020 | Publication des instantanés de diagnostics | sans constat établi |  |
| LSP-021 | Conversion des positions et modifications | sans constat établi |  |
| LSP-022 | Conversion des données JSON métier | constat | [WS-LSP-JSON-PRECISION](#finding-ws-lsp-json-precision) |
| LSP-023 | Contexte et reprise au curseur | constat | [WS-LSP-COMPLETION-TRIGGER-CONTEXT](#finding-ws-lsp-completion-trigger-context) |
| LSP-024 | Features de référence et candidats | sans constat établi |  |
| LSP-025 | Items, documentation et modifications | sans constat établi |  |
| LSP-026 | Correspondance approximative | sans constat établi |  |
| LSP-027 | Définition | sans constat établi |  |
| LSP-028 | Références et occurrences locales | sans constat établi |  |
| LSP-029 | Préparation et éditions de renommage | sans constat établi |  |
| LSP-030 | Hiérarchie de symboles du document | sans constat établi |  |
| LSP-031 | Recherche et résolution de symboles du workspace | sans constat établi |  |
| LSP-032 | Repliement des objets et commentaires | constat | [WS-LSP-FOLDING-CANCEL-POLL](#finding-ws-lsp-folding-cancel-poll) |
| LSP-033 | Sélections progressives | sans constat établi |  |
| LSP-034 | Survol | constat | [WS-LSP-PLAINTEXT-MARKUP](#finding-ws-lsp-plaintext-markup) |
| LSP-035 | Sélection des régions et actions | constat | [LSP-FORMAT-KEYWORD-CASSE](#finding-lsp-format-keyword-casse) |
| LSP-036 | Arbitrage des espaces et retraits | constat | [LSP-FORMAT-PRIORITE-GAP](#finding-lsp-format-priorite-gap) |
| LSP-037 | Nœuds masqués et commentaires | constat | [LSP-FORMAT-HIDDEN-ESPACES](#finding-lsp-format-hidden-espaces), [LSP-FORMAT-COMMENT-IDEMPOTENCE](#finding-lsp-format-comment-idempotence) |
| LSP-038 | Admission document, plage et frappe | constat | [LSP-FORMAT-PLAGE-DEBUT](#finding-lsp-format-plage-debut) |
| LSP-039 | Classification et légende | constat | [LSP-SEMANTIC-LEGENDE-FIXE](#finding-lsp-semantic-legende-fixe) |
| LSP-040 | Encodage et découpage des spans | constat | [LSP-SEMANTIC-DELTA-EXTENSION](#finding-lsp-semantic-delta-extension) |
| LSP-041 | Actions issues des diagnostics et versions | constat | [WS-LSP-ACTION-DOCUMENTCHANGES-OVERWRITE](#finding-ws-lsp-action-documentchanges-overwrite) |
| LSP-042 | Provider de déclaration | constat | [WS-LSP-NAVIGATION-NULL-ERASED](#finding-ws-lsp-navigation-null-erased) |
| LSP-043 | Provider de définition de type | sans constat établi |  |
| LSP-044 | Provider d’implémentations | sans constat établi |  |
| LSP-045 | Aide à la signature | sans constat établi |  |
| LSP-046 | Indications intégrées | sans constat établi |  |
| LSP-047 | Liens du document | sans constat établi |  |
| LSP-048 | Code lenses | sans constat établi |  |
| LSP-049 | Préparation et parcours des appels | constat | [WS-LSP-HIERARCHY-ITEM-IDENTITY](#finding-ws-lsp-hierarchy-item-identity) |
| LSP-050 | Préparation et parcours des types | constat | [WS-LSP-HIERARCHY-ITEM-IDENTITY](#finding-ws-lsp-hierarchy-item-identity) |
| LSP-051 | Registre et exécution des commandes | sans constat établi |  |
| LSP-052 | Lecture de documents par schéma | sans constat établi |  |
| LSP-053 | Modes de synchronisation et contrat de sauvegarde | constat | [WS-LSP-SAVE-HOOKS](#finding-ws-lsp-save-hooks) |
| MCP-001 | Décodage et dispatch JSON-RPC | constat | [WS-MCP-REQUEST-ID-RANGE](#finding-ws-mcp-request-id-range) |
| MCP-002 | Négociation et catalogue de session | constat | [WS-MCP-CATALOG-DUPLICATE](#finding-ws-mcp-catalog-duplicate) |
| MCP-003 | Schémas, URI et positions | constat | [WS-MCP-URI-NONHIERARCHICAL](#finding-ws-mcp-uri-nonhierarchical) |
| MCP-004 | Prérequis de lecture d’un document | constat | [WS-MCP-ROUTE-EMPTY-SUCCESS](#finding-ws-mcp-route-empty-success) |
| MCP-005 | Identités et réservations temporaires | constat | [WS-MCP-SCRATCH-IDENTITY-RETENTION](#finding-ws-mcp-scratch-identity-retention), [WS-MCP-SCRATCH-RELEASE-ALLOCATION](#finding-ws-mcp-scratch-release-allocation) |
| MCP-006 | Validation d’un texte éphémère | constat | [WS-MCP-VALIDATE-READ-ADMISSION](#finding-ws-mcp-validate-read-admission), [WS-MCP-VALIDATE-SHARED-EXPORTS](#finding-ws-mcp-validate-shared-exports), [WS-MCP-VALIDATE-CLEANUP-FAILURE](#finding-ws-mcp-validate-cleanup-failure), [WS-MCP-VALIDATE-CATEGORIES](#finding-ws-mcp-validate-categories) |
| MCP-007 | Lecture des diagnostics | constat | [WS-MCP-ROUTE-EMPTY-SUCCESS](#finding-ws-mcp-route-empty-success) |
| MCP-008 | Documents détenus et contenu courant | constat | [WS-MCP-DOCUMENT-SOURCE-LABEL](#finding-ws-mcp-document-source-label) |
| MCP-009 | Recherche de symboles exportés | constat | [WS-MCP-BOUND-LANGUAGE](#finding-ws-mcp-bound-language), [WS-MCP-SYMBOLS-MISSING-CST](#finding-ws-mcp-symbols-missing-cst) |
| MCP-010 | Plan d’un document | constat | [WS-MCP-OUTLINE-UNNAMED-CONTAINERS](#finding-ws-mcp-outline-unnamed-containers), [WS-MCP-OUTLINE-DEPTH](#finding-ws-mcp-outline-depth) |
| MCP-011 | Références d’un nom | constat | [WS-MCP-WORKSPACE-REFERENCE-BARRIER](#finding-ws-mcp-workspace-reference-barrier), [WS-MCP-ROUTE-EMPTY-SUCCESS](#finding-ws-mcp-route-empty-success) |
| MCP-012 | Proposition de renommage | constat | [WS-MCP-RENAME-QUADRATIC-DEDUP](#finding-ws-mcp-rename-quadratic-dedup), [WS-MCP-WORKSPACE-REFERENCE-BARRIER](#finding-ws-mcp-workspace-reference-barrier), [WS-MCP-ROUTE-EMPTY-SUCCESS](#finding-ws-mcp-route-empty-success) |
| MCP-013 | Description d’un élément au curseur | constat | [WS-MCP-BOUND-LANGUAGE](#finding-ws-mcp-bound-language), [WS-MCP-ROUTE-EMPTY-SUCCESS](#finding-ws-mcp-route-empty-success) |
| MCP-014 | Éléments attendus à une position | constat | [WS-MCP-ROUTE-EMPTY-SUCCESS](#finding-ws-mcp-route-empty-success) |
| MCP-015 | Candidats d’une référence attendue | constat | [WS-MCP-SCOPE-CONTEXT](#finding-ws-mcp-scope-context), [WS-MCP-SCOPE-FIRST-SITE](#finding-ws-mcp-scope-first-site), [WS-MCP-ROUTE-EMPTY-SUCCESS](#finding-ws-mcp-route-empty-success) |
| MCP-016 | Langues, grammaires et règles | sans constat établi |  |
| MCP-017 | Session MCP sur stdio | constat | [WS-MCP-STDIO-DISK-SNAPSHOT](#finding-ws-mcp-stdio-disk-snapshot) |
| MCP-018 | Tunnel MCP dans LSP | sans constat établi |  |
| MCP-019 | Production du texte formaté | constat | [WS-MCP-FORMAT-DOCUMENT-URI](#finding-ws-mcp-format-document-uri) |
| PUB-001 | Contrat du parseur effacé | sans constat établi |  |
| PUB-002 | Format et classement des erreurs de parsing | sans constat établi |  |
| PUB-003 | Association des commentaires à un nœud | sans constat établi |  |
| PUB-004 | Analyse des commentaires documentaires | sans constat établi |  |
| PUB-005 | Valeurs JSON du noyau et propriété de leur contenu | constat | [CORE-JSON-UTF8](#finding-core-json-utf8) |
| PUB-006 | Publication concurrente des observations | sans constat établi |  |
| PUB-007 | Rendu et identification des observations | constat | [CORE-OBS-NUL](#finding-core-obs-nul) |
| PUB-008 | Contrat des callbacks non propriétaires | sans constat établi |  |
| PUB-009 | Composantes fortement connexes génériques | sans constat établi |  |
| PUB-010 | Distance d'édition bornée | sans constat établi |  |
| PUB-011 | Rendu Markdown des commentaires documentaires | sans constat établi |  |
| PUB-012 | Résolution des liens documentaires | sans constat établi |  |
| REF-001 | Nom d’une déclaration et description exportable | sans constat établi |  |
| REF-002 | Adressage structurel d’un nœud | sans constat établi |  |
| REF-003 | Sélection des symboles exportés | sans constat établi |  |
| REF-004 | Attribution des scopes locaux | sans constat établi |  |
| REF-005 | Résolution d’un nom dans les scopes | constat | [WS-REF-CASEFOLD-ASCII](#finding-ws-ref-casefold-ascii) |
| REF-006 | Énumération des candidats de scope | constat | [WS-REF-SCOPE-EXTENSION-DRIFT](#finding-ws-ref-scope-extension-drift) |
| REF-007 | Conversion des candidats en résultat de linking | constat | [WS-REF-DESCRIPTION-NODE-REQUIRED](#finding-ws-ref-description-node-required) |
| REF-008 | Forçage et remise à zéro du linking | sans constat établi |  |
| REF-009 | Publication d’un résultat de référence | constat | [WS-REF-ERROR-CAUSE-LOST](#finding-ws-ref-error-cause-lost) |
| REF-010 | Attente et détection des cycles de résolution | sans constat établi |  |
| REF-011 | Mémorisation des noms consultés | sans constat établi |  |
| REF-012 | Production des descriptions de références | sans constat établi |  |
| REF-013 | Recherche d’une déclaration à une position | sans constat établi |  |
| REF-014 | Recherche des usages et auto-déclarations | constat | [WS-REF-OVERLOAD-SPAN-BEGIN](#finding-ws-ref-overload-span-begin) |
| SCH-001 | Choix du nombre de workers | sans constat établi |  |
| SCH-002 | Découpage des ranges et des lots | sans constat établi |  |
| SCH-003 | Issue d’un parallelFor interrompu | constat | [WS-SCH-CANCEL-LINEAR](#finding-ws-sch-cancel-linear) |
| SCH-004 | Durée de vie d’un job et de ses parts | sans constat établi |  |
| SCH-005 | Réservation des tranches de travail | constat | [WS-SCH-PRIVATE-SLICE](#finding-ws-sch-private-slice) |
| SCH-006 | Distribution des publications externes | constat | [WS-SCH-EXTERNAL-CONVOY](#finding-ws-sch-external-convoy) |
| SCH-007 | Recherche de travail et parking ordinaire | sans constat établi |  |
| SCH-008 | Construction et arrêt du pool | sans constat établi |  |
| SCH-009 | Attente coopérative d’un job imbriqué | constat | [WS-SCH-NESTED-SPIN](#finding-ws-sch-nested-spin) |
| SCH-010 | Arbitrage d’une deque entre propriétaire et voleurs | sans constat établi |  |
| SCH-011 | Croissance et rétention des tableaux de deque | sans constat établi |  |
| SCH-012 | Handshake atomique de réveil | sans constat établi |  |
| SCH-013 | Verrou court et attente active | sans constat établi |  |
| SVC-001 | Ownership des conteneurs de services | sans constat établi |  |
| SVC-002 | Installation des services par défaut | sans constat établi |  |
| SVC-003 | Construction typée d’un langage | sans constat établi |  |
| SVC-004 | Contrôle des slots requis | constat | [WS-SVC-SHARED-MISMATCH](#finding-ws-svc-shared-mismatch) |
| SVC-005 | Enregistrement et remplacement d’un langage | constat | [WS-SVC-REGISTRATION-PARTIAL](#finding-ws-svc-registration-partial) |
| SVC-006 | Sélection des services d’un document | sans constat établi |  |
| TXT-001 | Validation et décodage UTF-8 | sans constat établi |  |
| TXT-002 | Case folding et classes Unicode | constat | [WS-REF-CASEFOLD-ASCII](#finding-ws-ref-casefold-ascii) |
| TXT-003 | Génération et bornes des tables Unicode | constat | [CORE-UNICODE-OUTPUT-PATH](#finding-core-unicode-output-path), [CORE-UNICODE-VERSION](#finding-core-unicode-version) |
| TXT-004 | Index des lignes d'un TextDocument | constat | [CORE-TEXT-SIZE-DOMAIN](#finding-core-text-size-domain) |
| TXT-005 | Conversion offset octets vers position éditeur | sans constat établi |  |
| TXT-006 | Application ordonnée d'éditions textuelles | sans constat établi |  |
| TXT-007 | Construction d'une traduction source | sans constat établi |  |
| TXT-008 | Construction de texte et indentation | sans constat établi |  |
| TXT-009 | Canonicalisation des URI | constat | [CORE-URI-LOCALHOST](#finding-core-uri-localhost) |
| TXT-010 | Parenté et chemins relatifs d'URI | constat | [CORE-URI-TRIE-DEPTH](#finding-core-uri-trie-depth) |
| TXT-011 | Système de fichiers local | constat | [WS-WSP-SYMLINK-INCLUSION](#finding-ws-wsp-symlink-inclusion) |
| TXT-012 | Systèmes de fichiers mémoire et vide | sans constat établi |  |
| VAL-001 | Enregistrement et indexation des checks | constat | [WS-VAL-REGISTRY-HASH-EXCEPTION](#finding-ws-val-registry-hash-exception) |
| VAL-002 | Dispatch et exception d’un check | sans constat établi |  |
| VAL-003 | Pipeline documentaire et ordre des diagnostics | sans constat établi |  |
| VAL-004 | Diagnostics de parse et actions de réparation | sans constat établi |  |
| VAL-005 | Diagnostics de linking et suggestions orthographiques | constat | [WS-VAL-SPELLING-QUADRATIC](#finding-ws-val-spelling-quadratic) |
| VAL-006 | Hooks avant et après validation AST | sans constat établi |  |
| VAL-007 | Sélection et élagage des nœuds à valider | constat | [WS-VAL-OPTIONS-OPTIN](#finding-ws-val-options-optin) |
| VAL-008 | Construction et émission d’un diagnostic | constat | [WS-VAL-MISSING-ANCHOR-RANGE](#finding-ws-val-missing-anchor-range), [WS-VAL-ANCHOR-NOEXCEPT](#finding-ws-val-anchor-noexcept) |
| VAL-009 | Mémoire des catégories déjà validées | constat | [WS-VAL-CATEGORY-COMMIT](#finding-ws-val-category-commit) |
| VAL-010 | Exécution parallèle et collecte des checks AST | sans constat établi |  |
| WSP-001 | Création d’un document et choix de langage | sans constat établi |  |
| WSP-002 | Rafraîchissement d’un document existant | sans constat établi |  |
| WSP-003 | Rattachement du buffer et de sa version | sans constat établi |  |
| WSP-004 | Identité stable des documents | sans constat établi |  |
| WSP-005 | Insertion et récupération dans le store | sans constat établi |  |
| WSP-006 | Invalidation et suppression documentaire | sans constat établi |  |
| WSP-007 | Conservation d’une demande de build | constat | [WS-WSP-PENDING-OPTIONS](#finding-ws-wsp-pending-options) |
| WSP-008 | Exécution et publication des étapes d’une phase | sans constat établi |  |
| WSP-009 | Retour à un état antérieur du build | sans constat établi |  |
| WSP-010 | Installation d’un lot de changements workspace | sans constat établi |  |
| WSP-011 | Attente de l’état d’un document | sans constat établi |  |
| WSP-012 | Abonnements et livraison des événements de build | sans constat établi |  |
| WSP-013 | Admission et sortie d’une écriture workspace | sans constat établi |  |
| WSP-014 | Admission d’une lecture workspace | constat | [WS-WSP-READ-REENTRANCE](#finding-ws-wsp-read-reentrance), [WS-WSP-PRIORITY-STARVATION](#finding-ws-wsp-priority-starvation) |
| WSP-015 | Frontière durable et annulation d’une mutation | sans constat établi |  |
| WSP-016 | Fermeture du verrou workspace | sans constat établi |  |
| WSP-017 | Publication des symboles exportés | constat | [WS-WSP-INDEX-RETIRE-ORDER](#finding-ws-wsp-index-retire-order) |
| WSP-018 | Mise à jour des références indexées | sans constat établi |  |
| WSP-019 | Détection des documents affectés | sans constat établi |  |
| WSP-020 | Construction du snapshot global d’exports | sans constat établi |  |
| WSP-021 | Lecture de l’index des références entrantes | sans constat établi |  |
| WSP-022 | Construction et gel des symboles locaux | constat | [WS-WSP-LOCAL-NULL-CONTAINER](#finding-ws-wsp-local-null-container) |
| WSP-023 | Recherche dans les symboles locaux | sans constat établi |  |
| WSP-024 | Sélection des ressources d’un workspace | constat | [WS-WSP-SYMLINK-INCLUSION](#finding-ws-wsp-symlink-inclusion) |
| WSP-025 | Initialisation et disponibilité du workspace | sans constat établi |  |
| WSP-026 | Attente d’une publication du workspace | constat | [WS-WSP-WAIT-CANCELLED-BUILD](#finding-ws-wsp-wait-cancelled-build) |
| WSP-027 | Construction paresseuse des shards de noms exportés | sans constat établi |  |
| WSP-028 | Sélection et déduplication du travail d’une phase | sans constat établi |  |
| WSP-029 | Barrières entre scopes, linking et publication du contenu | sans constat établi |  |

## État de validation documentaire

Les IDs sont uniques et couvrent le catalogue513 ; les96 comparaisons requises possèdent une conclusion et des preuves. Les chemins de chaque preuve existent dans les snapshots et leurs numéros sont dans les fichiers. Les renvois entre constats ont été résolus et les causes communes consolidées. Les API/hôtes, les réserves et les doublons ont fait l’objet d’une relecture indépendante. Ces contrôles documentaires ne constituent pas une validation dynamique du programme.
