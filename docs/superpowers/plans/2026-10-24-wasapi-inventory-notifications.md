# Rafraîchissement automatique de l'inventaire WASAPI

Suite à la question utilisateur sur la découverte des périphériques : le probe
réel du 24 octobre trouve 132 endpoints, dont 22 actifs. L'API
GET /api/v1/wasapi-devices énumère déjà capture/render, états et formats. La GUI
lit l'état toutes les cinq secondes, mais celui-ci conservait un inventaire
mis en cache jusqu'à un nouvel appel explicite à l'énumérateur.

## Décision et réalisation

Utiliser IMMNotificationClient pour demander une nouvelle énumération sur le
worker du contrôleur, après la première demande d'inventaire. Les cinq callbacks
(ajout, retrait, état, propriété et défaut) ne font qu'un store atomique. Ils ne
lisent pas les identifiants, n'attendent pas, n'allouent pas et ne possèdent aucun
pointeur vers le contrôleur. Un booléen partagé possède sa propre durée de vie.

Le worker initialise un appartement STA qui reste vivant jusqu'après la
destruction de l'hôte ASIO et la désinscription de l'observateur. Les flux WASAPI
restent sur leurs workers MTA existants. L'inventaire est publié par SetStatus,
donc exposé par l'état API que la GUI relit déjà. Aucun contrat API ni choix de
périphérique/routage n'est modifié automatiquement.

Les rafraîchissements déclenchés par événement sont espacés d'au moins 500 ms
après la fin du précédent scan. Un événement arrivant pendant l'énumération
reste en attente. Si l'inscription échoue ou si l'inventaire retourne une erreur,
une nouvelle tentative est programmée après cinq secondes. Ces intervalles
sont des règles d'ordonnancement du worker, pas des garanties temps réel.
Le rafraîchissement fonctionne également quand l'audio est arrêté.

Stop désinscrit avant de libérer le callback. Une erreur inattendue de
désinscription conserve volontairement le callback autonome et son booléen
plutôt que de laisser Windows appeler un objet libéré ; cette branche peut
retenir cette petite allocation et n'est pas injectée par les tests actuels.

## Vérification

WasapiDeviceNotificationsTests exerce les cinq callbacks, QueryInterface,
coalescence et durée de vie indépendante du consommateur. Une inscription puis
désinscription réelles auprès de Windows réussissent. Aucun branchement physique
n'est simulé par ce succès. Le test de rafale joint le producteur avant lecture :
ce n'est pas une preuve de concurrence production/consommation.

AudioControllerWasapiTests --inventory-probe vérifie l'inventaire sur un worker
inactif après abonnement : l'appel manuel publie une première fois, puis le
changement initial en attente doit produire une seconde publication sans
modifier l'état audio. Les tests existants du contrôleur, de l'API des ponts et
de l'horloge doivent également passer après raccordement.

Résultats du 24 octobre : compilation du moteur et des quatre exécutables de
test réussie. WasapiDeviceNotificationsTests, AudioControllerWasapiTests,
WasapiBridgeApiTests et AudioControllerClockTests passent. Le probe explicite
--inventory-probe réussit avec 132 endpoints et state=stopped, après les deux
publications attendues. Pas de redémarrage ni de packaging de l'application
installée ; ces résultats portent sur les exécutables de build_codex_110/Release.

Revue indépendante : aucun défaut important de durée de vie, concurrence ou
modèle COM trouvé. Les délais 500 ms/5 s, le changement pendant un scan et le
branchement/débranchement physique restent à mesurer par des essais dédiés.
L'énumération automatique ne qualifie pas l'isolation du broker natif ni les
performances audio de l'objectif global.

Contrats Microsoft :
[callbacks non bloquants](https://learn.microsoft.com/en-us/windows/win32/api/mmdeviceapi/nn-mmdeviceapi-immnotificationclient),
[désinscription et durée de vie](https://learn.microsoft.com/en-us/windows/win32/api/mmdeviceapi/nf-mmdeviceapi-immdeviceenumerator-unregisterendpointnotificationcallback).
