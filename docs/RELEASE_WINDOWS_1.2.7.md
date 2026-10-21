# Livrables Windows 1.2.7

Version stable : [v1.2.7](https://github.com/timox/TimoxVasio/releases/tag/v1.2.7).

- `Timox VASIO Control Setup 1.2.7.exe` : interface, moteur et installation du pilote.
- `Timox VASIO Control 1.2.7.exe` : portable, sans installation du pilote.
- `TimoxVasio Driver 1.2.7.zip` : pilote et scripts d'installation manuelle.
- `MANIFEST.json` : commit source, horodatages et empreintes SHA-256.

La branche stable part du commit publié `f8984be` de la 1.2.7-test. Seuls le bouton de réorganisation, ses tests, la version et la documentation de publication sont ajoutés. Les travaux WASAPI du dossier de développement ne sont pas inclus.

## Vérifications

- 53 tests React et 5 tests du contrat API réussis ; 16 tests ciblés repassés après les derniers ajustements du bouton.
- Construction React de production réussie.
- Tests natifs réussis : AsioSampleConversionTests, AsioSampleConversionBlockTests (18 formats bit pour bit), RoutingGraphTests, AudioRoutingRuntimeTests, PerformanceStageMeasurementsTests, RealtimeCallbackSlotTests, AudioRoutingHotSwapTests et DriverCompatibilityProfileTests.
- Moteur et pilote reconstruits en Release x64 avec ProductVersion `1.2.7`.
- Pré/postflight obligatoires : comparaison du moteur extrait du Setup et du portable au build canonique ; comparaison du pilote extrait du Setup et du ZIP au build canonique.

Les tests d'installation et de cadence de la 1.2.7-test sont conservés dans les résultats locaux. Cette publication ne remplace pas automatiquement la version installée sur le PC.
