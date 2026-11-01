# Qualification de l'admission et de l'isolation audio native

État au 22 octobre 2026 : **politique pure testée sur identités synthétiques ; isolation audio Windows non qualifiée**. Aucun essai de ce document n'a été exécuté sur l'hôte de développement. Aucun pilote, service, certificat, ACL ou périphérique par défaut n'est modifié par les tests de politique.

## Contrat de la politique pure

Reproduction des tests sans SDK ASIO ni WDK, depuis la racine vérifiée :

```powershell
cmake -S . -B build_native_production -DVASIO_BUILD_NATIVE_PRODUCTION_TESTS_ONLY=ON
cmake --build build_native_production --config Release
ctest --test-dir build_native_production -C Release --output-on-failure
```

Validation parent : compilation MSVC `/W4 /WX` réussie et CTest 1/1 réussi après revue indépendante. Cette réussite concerne seulement la politique pure ci-dessous.

`EvaluateNativeAdmission` consomme un instantané de confiance préparé hors callback. Les SID doivent provenir de jetons Windows vérifiés, sous forme canonique ; ils ne proviennent jamais d'un JSON, d'un paquet N0 ou d'une déclaration du client. Cette fonction ne vérifie ni la provenance, ni la syntaxe des SID, ni l'authentification TLS. Les tests ne démontrent pas l'extraction des jetons Windows.

La politique initiale exige le compte explicitement autorisé, une seule session de logon interactive, la console déverrouillée, l'identité complète du logon console courant et une isolation d'endpoint indépendamment qualifiée. `interactiveLogonCount` doit compter aussi les logons interactifs déconnectés conservés ; une session RDP ne devient pas admissible parce qu'elle est seule. L'identité comprend SID utilisateur, SID de logon, AuthenticationId et SessionId. Le seul numéro SessionId ne suffit pas : la fonction compare aussi l'identité console complète et celle du bail existant.

L'ordre des refus est déterministe : identité/observation incomplète, compte non autorisé, session hors périmètre, console verrouillée, isolation non qualifiée, puis différence de logon. `Allowed` signifie admissibilité de cette identité selon cet instantané, pas acquisition d'un bail. Une seconde connexion avec la même identité doit toujours recevoir `OwnedElsewhere` du gestionnaire de bail tant que le propriétaire existe.

`EvaluateNativeLeaseContinuation` réévalue l'identité originale du bail. Il retourne `Retire` avec la cause si les conditions changent, y compris si le bail n'est plus enregistré (`LeaseNotActive`). Il ne réécrit pas l'identité du bail et ne le transfère jamais. Le consommateur doit bloquer les nouvelles admissions, retirer les références du graphe, attendre sa quiescence, annuler/attendre les IO, fermer le propriétaire et purger/retirer l'endpoint avant une nouvelle admission. Les décisions pures ne réalisent aucune de ces opérations.

Une décision ne ferme pas la course entre lecture des événements de session et ouverture d'un stream. L'exclusivité console et les notifications WTS ne constituent **pas** une preuve d'isolation hostile. Le booléen `audioIsolationQualified` reste faux pour tout accès réel tant que les gates ci-dessous ne sont pas satisfaites. Aucune réussite de tests fake, de TLS ou de politique ne peut le positionner.

## Observations Windows à établir sur cible

`broker/session_observation_win32.cpp` observe maintenant, en lecture seule, la session console, le nombre conservateur de sessions interactives (y compris les sessions déconnectées), le SID du compte console résolu depuis le nom WTS, et l'état verrouillé/déverrouillé. Toute observation incomplète échoue sans modifier le résultat fourni ; les états WTS inconnus bloquent l'admission. L'acceptation du socket vérifie ensemble ce SID, la session console physique, l'unicité de la session interactive, l'état déverrouillé et l'identité complète du jeton primaire du processus pair avant d'attacher le broker. Cette chaîne n'est pas encore appelée par un service.

Avant l'admission, `SocketSession` lit le PID pair fourni par Winsock, conserve un handle de processus et compare son jeton primaire (SID utilisateur, SID de logon, AuthenticationId et SessionId) à l'identité déjà enrôlée dans le TLS. Il exige aussi que ce SID et ce SessionId correspondent au compte et à la session console physique déverrouillée observés par WTS, avec une seule session interactive. Pendant le bail, le superviseur réobserve toutes les 200 ms l'unicité des sessions interactives, le verrouillage et l'identifiant console ; une observation manquante ou un changement annule le socket et l'opération noyau en attente. Cette vérification périodique omet la résolution du nom de compte, qui n'est faite qu'à l'admission. Le processus doit rester vivant pendant le bail. Cela empêche une déclaration d'identité TLS seule ou une session RDP de passer l'admission ; le PID reste une observation du créateur de la connexion, pas la preuve du détenteur courant d'un socket dont le handle aurait été transféré. L'isolation audio reste non qualifiée. [WSAIoctl](https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-wsaioctl).

Le collecteur d'identité doit encore associer le certificat client au SID réellement observé par le service, puis relever TokenUser, le groupe de logon, TokenSessionId et TokenStatistics.AuthenticationId. AuthenticationId identifie le logon, tandis que TokenId identifie une instance de jeton : ils ne sont pas interchangeables. [Microsoft TOKEN_STATISTICS](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-token_statistics). La documentation WTS confirme que `WTSDisconnected` est une session utilisateur encore active mais déconnectée, et que `WTSSessionInfoEx` fournit les flags verrouillé/déverrouillé ; les flags sont refusés sur Windows 7/Server 2008 R2 à cause de leur inversion documentée. [WTS_CONNECTSTATE_CLASS](https://learn.microsoft.com/en-us/windows/win32/api/Wtsapi32/ne-wtsapi32-wts_connectstate_class), [WTSINFOEX_LEVEL1](https://learn.microsoft.com/en-us/windows/win32/api/wtsapi32/ns-wtsapi32-wtsinfoex_level1_w).

`WTSGetActiveConsoleSessionId` peut retourner `0xFFFFFFFF` pendant une transition. Cela doit produire un refus, jamais la réutilisation silencieuse de la dernière observation. Cette API fournit un numéro de session, pas l'identité complète exigée. [Microsoft WTSGetActiveConsoleSessionId](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-wtsgetactiveconsolesessionid).

L'acquisition indépendante de l'identité console par le service LocalService reste à concevoir et qualifier. `WTSQueryUserToken` requiert LocalSystem et SeTcbPrivilege ; elle ne doit pas être appelée en supposant ces droits disponibles, ni justifier une élévation automatique de LocalService. Une observation indisponible laisse `optionalConsoleIdentity` absent et l'admission refusée. [Microsoft WTSQueryUserToken](https://learn.microsoft.com/en-us/windows/win32/api/wtsapi32/nf-wtsapi32-wtsqueryusertoken).

Le loopback peut inclure de l'audio de plusieurs sessions Terminal Services. Ajouter une étiquette de session après mélange ne sépare pas les sources. Cette documentation motive la qualification ; elle ne démontre pas le comportement du miniport Timox non chargé. [Microsoft Loopback Recording](https://learn.microsoft.com/en-us/windows/win32/coreaudio/loopback-recording).

## Banc cible à préparer, sans l'exécuter ici

Utiliser une machine ou VM récupérable dédiée, avec console de secours, snapshot initial, versions Windows/WDK consignées et hashes SHA-256 de chaque binaire. Préparer deux comptes ordinaires A/B, deux logons du compte A lorsque l'édition Windows le permet, console et RDP. Si une combinaison n'est pas disponible sur l'édition testée, la marquer **non exécutée**, sans la remplacer par un test fake.

Chaque émetteur produit une séquence PCM déterministe distincte, indexée par compte, logon, stream et essai. Utiliser des motifs à corrélation connue et un identifiant de bloc ; un simple sinus partagé entre comptes ne permet pas l'attribution. Capturer le PCM brut avec horodatage, direction et epoch du récepteur, ainsi que les erreurs d'ouverture. Conserver les résultats par récepteur : l'absence globale de son ne suffit pas à distinguer un refus correct d'un banc inactif.

Mesurer d'abord le contrôle positif autorisé pour chaque mode et calibrer la détection des motifs après les conversions réellement utilisées. Sur un chemin PCM exact, vérifier les mots et indices de blocs ; avec conversion, fixer les seuils de corrélation à partir du contrôle positif et du silence avant les essais hostiles, sans les ajuster après un échec. L'exigence reste aucune présence détectable d'une séquence étrangère ; consigner la limite de détection de l'instrumentation.

| Scénario | Ouvertures et transitions | Résultat requis |
|---|---|---|
| A console seul, autorisé | WASAPI partagé render/capture | Contrôle positif PCM bidirectionnel, seulement après isolation qualifiée sur le banc |
| Compte B | Ouvertures avant et pendant le bail A | Aucune lecture/injection du PCM A ; refus ou endpoint propre à B |
| A dans un autre logon | Même SID utilisateur, logon SID/LUID différents | Aucun transfert de bail ni PCM partagé |
| RDP et console | A puis B dans les deux ordres | Pas d'admission hors périmètre ; aucune fuite dans les streams déjà ouverts |
| Shared/exclusive/KS | Tester chaque mode accessible dans chaque logon | Aucun contournement de la frontière endpoint ; noter les modes réellement non pris en charge |
| Flux préouverts | Ouvrir render/capture avant création, retrait puis remplacement du bail | Ancien handle incapable de lire/injecter le PCM du nouveau propriétaire |
| Verrouillage/déconnexion/logoff | Transition pendant lecture et écriture, puis nouvelle connexion | Retrait explicite, arrêt complet avant nouvelle admission |
| Réutilisation SessionId | Nouveau logon du même compte avec ancien client encore vivant si possible | Ancien LUID/logon refusé malgré le même numéro de session |
| Reprise service/pilote | Crash/annulation pendant IO, puis nouvelle instance | Aucun double owner, aucune completion tardive utilisant une mémoire libérée |
| Protection de contenu | Flux protégé, changement de droits, teardown/reopen | Aucun PCM protégé remis au broker ; refus distingué de périphérique absent |

Pour chaque transition, garder une capture continue couvrant l'avant, l'événement et la reprise ; répéter en variant la phase par rapport aux paquets et quanta. Enregistrer les files N0, le ring DMA et les completions en attente sur le banc instrumenté. Rechercher le dernier motif de l'ancien logon dans toutes les captures après attribution au nouveau propriétaire. Une augmentation d'epoch ou une file logicielle vide ne prouve pas que le DMA et les handles préouverts sont isolés.

## Mesure de la frontière d'ouverture et décision d'architecture

Tracer l'identité réellement vue à la création du filtre/pin et du stream : initiateur original, intermédiaire audio, droits demandés, device/interface concerné, SessionId et LUID observables. Comparer WASAPI partagé, exclusif et KS ; ne pas supposer que le jeton vu par PortCls est celui de l'application originale.

Si l'ACL est évaluée contre un intermédiaire SYSTEM qui dessert plusieurs logons, son succès ne qualifie pas une ACL utilisateur du device comme barrière audio. Ne pas ajouter SYSTEM partout pour faire passer le test. Produire un plan fils pour un endpoint réellement propre au logon, avec DDI d'admission/visibilité identifiées et sources Microsoft précises, puis reprendre la matrice. **Aucun chemin DDI d'isolation n'est encore retenu ni déclaré démontré dans ce lot.**

## Rapport exigé avant qualification

Le rapport doit contenir : OS/édition/build, hashes et chemins des binaires, comptes/logons pseudonymisés et leurs identités comparées, configuration du banc, modes ouverts/refusés et codes exacts, chronologie des transitions, traces d'ownership/annulation/quiescence, captures brutes et résultat du détecteur par cas. Ne jamais enregistrer de credential, clé privée ou bearer ; les captures de banc ne contiennent que les séquences synthétiques autorisées.

Statut par cas : PASS, FAIL ou NOT RUN. Un cas hostile non exécuté ne vaut pas PASS. Gate globale : tous les cas applicables passent, aucune ouverture préexistante ne donne accès au nouveau logon, aucune séquence étrangère détectée, et mécanisme endpoint expliqué/qualifié. Un échec bloque l'activation publique ; il ne justifie pas l'assouplissement de la politique.

Le test C++ `NativeAudioAccessPolicyTests` prouve uniquement les refus et décisions de retrait pour des instantanés synthétiques. Le collecteur OS, la course des notifications, le broker, l'IPC, l'installation et l'isolation PCM réelle restent hors de cette preuve. Aucun script d'installation ou d'essai matériel n'est fourni par ce sous-lot.
