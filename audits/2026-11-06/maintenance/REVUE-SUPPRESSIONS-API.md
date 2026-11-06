# Relecture des suppressions et des frontières d’API

Source relue : `ydaveluy/cursiv`, commit `66c465bba3d2abd655e0e8fad3063f1fcfa9d599`.
Les chemins et numéros de ligne ci-dessous désignent cette révision. Cette note complète les [fiches consolidées](resultats-consolides.json) ; elle ne modifie ni leurs résultats ni les sources du projet.

Méthode : lecture statique des implémentations, signatures, consommateurs, scripts de packaging et de génération ; recherche des symboles et comparaison textuelle des copies. Aucun reproducer, build, test, benchmark ou programme du projet n’a été exécuté. Les vérifications proposées sont destinées à l’agent qui entreprendra les correctifs.

## Verdicts

| Constat | Verdict de relecture | Limite déterminante |
| --- | --- | --- |
| `M-PUB-JSON-FINITE-GUARD` | Confirmé | La branche privée est inaccessible dans les états construits par l’API ; la normalisation publique doit rester. |
| `M-AST-VALUE-AS` | Confirmé | Réutiliser le pointeur déjà vérifié en conservant la signature et les qualifications de `T`. |
| `M-HOST-MEMORY-READER` | Confirmé | Extraire le décodage et distribuer ses dépendances ; les politiques des deux hôtes diffèrent. |
| `M-HOST-MCP-COPIES` | Confirmé | Les cinq sources sont actives ; canoniser la source ne doit pas supprimer leur disponibilité dans les artefacts autonomes. |
| `M-HOST-EXTENSION-COPIES` | Confirmé | Les quatre exemples partagent une logique ; le template a des clés et une recherche de serveur différentes. |
| `M-BLD-PARSER-UNIT-COUNT` | Confirmé | `8` et `0..7` concordent aujourd’hui ; leur maintien commun concerne aussi le build hôte. |
| `M-LR-MULTI-UNUSED-UNION` | Confirmé | Retirer uniquement l’accumulateur local `modeAct` ; `reads` et `modeTakes` restent actifs. |
| `M-LEX-PARTITION-QUEUED` | Confirmé | Retirer uniquement les trois sites de `queued` ; ne pas changer la worklist. |
| `M-LL-MEMO-UNREACHABLE-TAIL` | Rétention confirmée ; compaction conditionnelle | Une ancienne ligne n’est plus recherchée, mais une copie publique de cette ligne peut encore servir à `lay`. |

Les six constats demandés en priorité dans les lots root ne sont pas réfutés. Les détails suivants précisent la portée de leurs corrections ; ils ne constituent pas de nouveaux défauts comptés dans la campagne.

## `M-PUB-JSON-FINITE-GUARD` — preuve de branche inaccessible

Dans `src/core/cursiv/services/JsonValue.cpp:64–72`, `serialize_number` est une fonction du namespace anonyme. Son seul appel se trouve dans le dernier bras de `append_json_value`, ligne 157. Le bras entier, lignes 148–149, intercepte déjà `int64_t` ; ce dernier bras reçoit donc le double de `number()`.

La seule construction directe d’un double dans `_value` est `JsonValue(double)`, lignes 171–172. Elle choisit `Storage(nullptr)` pour NaN et les infinis. Le getter `number()`, lignes 213–215, retourne un double par valeur : il ne permet pas de modifier le double stocké. Les constructeurs par copie et déplacement sont par défaut ; `operator=`, lignes 181–184, reçoit un autre `JsonValue` par valeur et déplace son stockage. Ils préservent l’invariant.

Le nom de type `Storage` est public (`JsonValue.hpp:31`), mais le membre `_value` est privé (`JsonValue.hpp:70–71`). Aucun constructeur depuis `Storage`, accès brut au membre, friend ou setter de double ne contourne la normalisation. Les accès mutables `array()` et `object()` exposent des conteneurs de `JsonValue`, pas les variantes internes de leurs éléments : leurs insertions et remplacements conservent donc le passage par les constructeurs/affectations précédents.

**Correction admise :** supprimer la garde `!std::isfinite` du sérialiseur privé ou la convertir en assertion de l’invariant. Conserver la normalisation du constructeur. Ne pas supprimer `<cmath>` : le constructeur en dépend toujours. Cela retire une redondance locale de faible coût ; aucun gain mesuré n’est établi.

**Vérification ultérieure :** confirmer que l’API n’a pas acquis entre-temps un accès mutable au stockage ; conserver les résultats publics pour NaN/infinis, nombres finis, copies et remplacements dans tableaux/objets. Les états produits par corruption mémoire ou usage indéfini ne sont pas des voies publiques justifiant la branche.

## `M-AST-VALUE-AS` — réemploi sûr du résultat vérifié

`src/core/cursiv/syntax-tree/ParseResult.hpp:84–88` appelle `ast_cast<T>(value)` pour tester le succès, puis recalcule le pointeur via `detail::ast_cast_unchecked<T>(value)`. `ast_cast`, dans `AstNode.hpp:763–770`, a déjà effectué ce dernier calcul avec `const T` après avoir vérifié pointeur, tag et relation de dérivation.

Pour les types non simples, `ast_cast_unchecked`, lignes 311–340, peut chercher la base dans le tag, lire un offset par `atomic_ref::load`, ou appeler `dynamic_cast` si aucun offset n’est disponible. Le second appel ne se réduit donc pas systématiquement à une opération purement syntaxique dans le source. Le gain à l’exécution dépend toutefois de l’instanciation et du compilateur.

Le membre `value` est un `AstNode*` public (`ParseResult.hpp:70`). Une méthode `const` rend le membre pointeur non modifiable par cette méthode, sans rendre son objet pointé const. Réutiliser le pointeur du cast vérifié puis retirer localement la qualification ajoutée par `ast_cast` peut ainsi conserver le `T*` de l’API. Une surcharge mutable de `ast_cast` serait une autre option, avec une surface de modification plus large.

**Correction admise :** un seul calcul vérifié, sans modifier `value_as() const -> T*`. Conserver `T` éventuellement const, pointeur nul, mauvais type, tag nul et ajustement des bases virtuelles. En particulier, ne pas remplacer le contrôle par le seul cast non vérifié : ce dernier possède des chemins de repli pour les tags absents, alors que `ast_cast` les refuse.

**Vérification ultérieure :** types retournés et usages existants avec héritage simple/virtuel et résultat vide. Ne pas transformer ce nettoyage local en changement de constance de l’API.

## `M-HOST-MEMORY-READER` — décodage commun, politiques distinctes

Le lecteur exporté de `tools/wasi-run.mjs:171–236` contrôle ses bornes, le LEB128 u32 et les flags wasm32 ; il sait passer les références typées et les imports de tags. `docs/demo/server.js:10–30` se dit identique mais emploie un décodage abrégé, passe les tables/globals autrement et ne gère pas les tags de la même façon. Il y a donc bien deux implémentations apparentées du même format à maintenir.

Leurs contrats hôtes ne sont pas identiques :

- Le runner renvoie `undefined` sans mémoire importée ; son appelant, `wasi-run.mjs:359–371`, distingue ce cas, refuse plusieurs mémoires et crée une mémoire avec le partage déclaré.
- La démo lève une erreur sans mémoire importée (`server.js:29`), substitue le minimum au maximum absent (ligne 21) et force `shared: true` (ligne 50).
- Les deux compilent le module avant de le décoder (`wasi-run.mjs:357`, `server.js:38`) ; cela n’unifie pas les formes d’imports que leurs décodeurs savent traverser.

**Correction admise :** partager un lecteur de bytes pur, puis garder explicitement les politiques de mémoire de chaque appelant. Conserver l’export `importedMemoryLimits` de `wasi-run.mjs` ou une réexportation compatible : des consommateurs du dépôt l’importent, notamment `tools/wasi-run.test.mjs:8–9` et `tools/demo-worker.test.mjs:10`.

**Frontière de distribution :** `tools/package-vsix.sh:43` ne copie actuellement que le module et `tools/wasi-run.mjs` comme code serveur JS ; `cmake/CursivGrammar.cmake:114–123` copie aussi ce runner dans les projets consommateurs. Ajouter un import relatif au runner implique soit d’embarquer le helper, soit de modifier ces deux listes. La démo utilise des modules JS par URL (`docs/demo/app.js:3`, `server.js:82`) : le helper doit également exister sous une URL publiée appropriée. Un import de la page vers le runner Node entier ne convient pas.

**Vérification ultérieure :** fichiers distribués, chemins relatifs/reexports et maintien des différences hôtes. Les formes mémoire/table/global/tag, l’absence de mémoire et le refus wasm32 doivent être explicitement couverts lors du correctif. La relecture ne transforme pas les divergences relevées en résultats d’exécution observés.

## `M-HOST-MCP-COPIES` — cinq sources actives

Comparaison statique confirmée : les quatre `examples/{arithmetics,domainmodel,requirements,statemachine}/vscode/src/mcp.ts` ont les mêmes octets, de SHA-256 `b5f117eb7bbdd65a88d2083ceb1600da80ffe8c415064d5eaa951e06a3e5e26b`. Le diff avec `templates/project/vscode/src/mcp.ts` ne modifie que des commentaires. L’import local et l’appel de `startMcpBridge` sont présents dans chaque extension (`extension.ts:16,201` pour les exemples ; `15,130` pour le template).

**Correction admise :** désigner une source canonique et produire des copies ou des bundles traçables. La suppression brute de quatre fichiers casserait des imports actuels. La contrainte d’autonomie des exemples est explicite dans `examples/domainmodel/vscode/src/extension.ts:1–3` ; les fichiers `esbuild.mjs` embarquent les dépendances dans `out/extension.js`, avec `vscode` externe. Un partage intégré au bundle peut fonctionner, mais doit aussi préserver la possibilité de construire le dossier exemple isolé.

Le projet généré exige une attention supplémentaire : `cmake/GenerateScaffold.cmake:17–24` intègre les fichiers du template dans `docs/cursiv-new.cmake`. Une extraction doit mettre le code nécessaire dans les fichiers émis ou dans leurs dépendances explicites ; une référence au checkout parent n’est pas disponible chez l’utilisateur. Le scaffold généré reste à régénérer au moment du correctif.

**À préserver :** transport HTTP existant, token par session, réponses de notification, fermeture du serveur et enregistrement MCP facultatif avant/après VS Code 1.101. L’autonomie interdit d’interpréter « duplication » comme « copies mortes ».

## `M-HOST-EXTENSION-COPIES` — paramétrer sans uniformiser les politiques

Les quatre exemples reprennent les mêmes fonctions de détection Wasm, construction de commande, recherche, activation et arrêt. Les identifiants, labels, noms de serveur et listes de langues sont paramétrés en tête (`extension.ts:18–21`). `requirements` sélectionne deux langues : `requirements-lang` et `tests-lang`.

Le template n’est pas une cinquième copie interchangeable. Il utilise `${ID}.server.path` (`templates/project/vscode/src/extension.ts:95–100`), cherche un serveur natif sous `bin/` puis `../build/src/` (lignes 78–90), et ne reprend pas la recherche des racines de dépôt des exemples. Ces derniers emploient `cursiv.${KEY}.serverPath`, la variable `CURSIV_<KEY>_SERVER`, le build du dépôt et les commandes sur PATH (`examples/domainmodel/vscode/src/extension.ts:129–153`). Leurs activations privilégient également configuration, preset et Wasm dans l’ordre explicite des lignes 159–166.

**Correction admise :** source canonique pour la mécanique commune et configuration/politiques explicites pour clés, langues, noms et chemins. Garder les quatre dossiers et le template constructibles/distribuables de façon autonome, via les mêmes contraintes de copie, génération, bundle et scaffold que pour MCP. Ne pas imposer au template la recherche spécifique d’un arbre `examples/`.

**Vérification ultérieure :** noms Windows, commande sur PATH, priorités de configuration, variables d’environnement, sélecteurs multi-langues, arguments/environnement Wasm et arrêt du client dans les cinq artefacts. La preuve de duplication est statique ; aucun démarrage d’extension n’a été réalisé.

## `M-BLD-PARSER-UNIT-COUNT` — deux déclarations d’un contrat de génération

`src/engines/cursiv/shared/gen/CommonArtifacts.hpp:21–24` impose huit unités, y compris des unités vides, précisément parce que le build annonce leurs noms avant exécution du générateur. `cmake/CursivGrammar.cmake:207–218` construit indépendamment `ParserRules0..7.cpp` ou `ParserGroups0..7.cpp`.

Cette constante est active dans les deux moteurs : `ll_emit_parser.cpp:1534–1539` calcule le budget et alloue les huit sources ; `lr_emit_parser.cpp:2586–2606` répartit les groupes dans huit unités, même quand certaines restent vides. La présence de sources vides n’est donc pas en soi une preuve de code généré mort : elle sert un contrat de build déterministe.

**Correction admise :** définition unique lisible lors de la configuration et par les générateurs C++, ou contrôle explicite de concordance si générer un header rendait le build plus complexe. Ce n’est pas une désynchronisation actuelle : `8` et `0..7` concordent. Le coût est celui du maintien de deux déclarations.

**Frontière hôte/cible :** `cmake/CursivGrammar.cmake:62–81` configure et construit un arbre natif pour les générateurs en cross-compilation ; `CURSIV_HOST_TOOLS` peut désigner un arbre existant. Toute définition générée doit être visible et cohérente dans cet arbre, pas uniquement dans le répertoire de build cible. Ne pas remplacer les outputs connus à la configuration par une découverte après génération sans revoir le contrat CMake.

## `M-LR-MULTI-UNUSED-UNION` — suppression locale confirmée

`src/engines/cursiv/lr/gen/lr_build_lex.cpp` contient exactement deux occurrences de `modeAct` : sa déclaration ligne 596 et son accumulation ligne 598. Aucune lecture ne dépend de son résultat.

Le lambda `reads`, lignes 434–438, copie `modes.trivia`, ajoute `modes.joins` puis les termes des actions dans un `DynSet` local. `LrBuildTable::action_row`, `LrBuildTable.hpp:383–385`, retourne un span const sans mutation. L’accumulation supprimable ne remplit donc pas indirectement un cache ou un état de diagnostic. Ses allocations et unions de sets ne contribuent pas au résultat du calcul.

**Correction admise :** retirer `modeAct` de la déclaration et `modeAct |= reads(f)`, puis adapter le commentaire associé. Conserver `modeTakes`, alimenté ligne 599 et lu ligne 640. Conserver `reads(f)` ligne 651, qui choisit le témoin de diagnostic et son ordre. Ne pas supprimer le lambda ni les types/includes encore employés ailleurs.

## `M-LEX-PARTITION-QUEUED` — pas de changement d’algorithme justifié

Dans `src/engines/cursiv/shared/gen/PartitionRefine.hpp`, `queued` apparaît uniquement en déclaration initialisée ligne 56, écriture par indice ligne 61 et `push_back` ligne 98. Son contenu n’est ni lu, ni transmis, ni capturé. La worklist opérationnelle est le vecteur distinct `work`, lignes 54–59 et 99.

**Correction admise :** supprimer les trois sites de `queued`. Ne pas remplacer cette suppression par une nouvelle logique de déduplication ou de choix du splitter : le constat ne démontre pas qu’il faudrait modifier l’algorithme de raffinement, ses compteurs `reads`/`moves` ou l’ordre des blocs.

Ce constat du lot LL/LEX a également été relu par un autre auditeur ; la présente vérification réinspecte les écritures et leur absence de consommateurs.

## `M-LL-MEMO-UNREACHABLE-TAIL` — limite à toute suppression d’historique

La rétention décrite est réelle : `LlEpisodeMemo::add`, `src/engines/cursiv/ll/runtime/LlEpisodeMemo.hpp:86–101`, ajoute sans éviction des lignes et plans ; `find_at`, lignes 122–132, ne consulte que les seize lignes les plus récentes par clé entière de 64 bits. Des données conservées ne sont donc plus candidates à cette recherche.

Cela ne prouve pas qu’elles soient inutilisables par toute l’API. `LlRemembered` est une structure copiable avec champs publics `plan` et `count` (lignes 20–35). `find` retourne un pointeur sur une ligne (lignes 75–82), tandis que `lay(const LlRemembered&, ...)`, lignes 104–111, lit les plans par ces indices. Un appelant peut copier le résultat avant des ajouts ultérieurs et demander encore son plan après qu’il est sorti de l’horizon de recherche. La stratégie append-only actuelle conserve les indices du plan copié ; la réallocation des lignes invalide les pointeurs directs, pas ces copies.

L’usage interne de l’épisode effectue un `find` puis `lay` rapprochés (`LlEpisode.hpp:432–453`), mais n’exclut pas ce scénario externe. L’include root des moteurs est exposé par `src/CMakeLists.txt:30–31` à travers la cible LL. Il ne faut pas déduire l’absence de clients externes de leur absence dans le checkout.

**Correction conditionnelle :** avant une compaction ou suppression des anciens plans, définir et préserver le contrat d’invalidation des pointeurs **et des copies** de `LlRemembered`, ou remplacer ces accès par des handles dont le contrat est explicite. Conserver l’ordre de préférence des candidats et la séparation des réponses dépendant de la fin du texte. Le rapport du lot a déjà intégré cette réserve après relecture indépendante.

La formulation précise reste « hors de l’horizon de recherche », et non « code ou données mortes supprimables sans condition ».
