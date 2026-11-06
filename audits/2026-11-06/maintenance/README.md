# Audit de maintenabilité — 513 périmètres

Nouvelle campagne statique du 6 novembre 2026 : **513 périmètres audités, 158 constats uniques**, dont **118 établis** et **40 candidats**. Elle porte sur le code mort, la duplication, la factorisation, les traces legacy, la simplification et l’optimisation.

La source examinée est [`66c465bba3d2abd655e0e8fad3063f1fcfa9d599`](https://github.com/ydaveluy/cursiv/tree/66c465bba3d2abd655e0e8fad3063f1fcfa9d599). Les preuves des rapports utilisent des liens vers cette révision. Aucun code source n’a été modifié, aucun reproducer, build, test ou benchmark n’a été exécuté dans cette campagne.

## Documents

- **[Synthèse autonome pour un agent correcteur](SYNTHESE-PASSATION-MAINTENABILITE.md)** : les 158 constats avec preuves, usages, conditions, changements proposés, invariants et validations futures.
- **[Rapport exhaustif](AUDIT-MAINTENABILITE-513.md)** : les constats détaillés et les 513 fiches de périmètre, y compris celles sans constat retenu. [Télécharger le Markdown brut](https://raw.githubusercontent.com/ydaveluy/cursiv/refs/heads/audit/audits/2026-11-06/maintenance/AUDIT-MAINTENABILITE-513.md).
- [Relecture des suppressions et factorisations des moteurs](REVUE-SUPPRESSIONS-MOTEURS.md).
- [Relecture des suppressions et frontières d’API](REVUE-SUPPRESSIONS-API.md).

## Données et contrôle documentaire

- [Catalogue des 513 périmètres](scopes.json).
- [Fiches consolidées](resultats-consolides.json).
- [Constats consolidés et périmètres associés](constats-consolides.json).
- [Validation documentaire et empreintes des fichiers publiés](VALIDATION-DOCUMENTAIRE.json).

Le manifeste de validation distingue les empreintes des livrables locaux originaux de celles des fichiers publiés. Les adaptations de publication concernent uniquement les mentions de diffusion, les chemins locaux et les renvois documentaires. Les empreintes des lots de travail d’origine sont conservées à titre de provenance ; leurs données sont intégralement reprises dans les deux fichiers consolidés.

Les contrôles vérifient la couverture, les identifiants, les références et les fichiers/lignes cités. Ils ne constituent pas des tests fonctionnels du projet, ni une mesure des gains proposés. Une observation établie ne signifie pas que son correctif est sans risque.

Les [rapports de l’audit précédent](../SYNTHESE-PASSATION.md), consacré au fonctionnel et aux API, sont conservés séparément.
