# Prototype Schannel en mémoire

Ce sous-projet de test autonome ne constitue pas le canal du broker. Il ne crée
aucun service, socket ou pipe et ne charge pas le pilote audio.

## Résultats réels — 22 octobre 2026

Compilation MSVC 19.51 x64 Release réussie avec `/W4 /WX`, SDK 10.0.26100.0.
Le mode explicite `--temporary-user-key` a terminé avec **PASS, code 0** :
handshake mutuellement authentifié TLS 1.2, suite
`TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384` (`0xc02c`), chiffrement duplex et
refus des cas négatifs décrits ci-dessous. Journal local :
`build_native_schannel_probe/temporary-user-key-final.log`.
Il a créé trois clés utilisateur non exportables et confirmé **3 suppressions,
3 réouvertures refusées avec `NTE_BAD_KEYSET`**. Aucune clé privée n'est exportée
dans le dépôt ou les journaux.

Binaire testé : `build_native_schannel_probe/Release/NativeAudioSchannelProbe.exe`.
SHA-256 : `762DFC0CCA55BD7DC143EF3F4B6D7248EE09967F46D335EC573C1E6D2920D3F8`.

En mode éphémère, la création CNG P-256, l'association certificat/clé et les
prédicats d'empreinte/validité/EKU réussissent. L'essai initial hors sandbox
a cependant obtenu `0x8009030D` (`SEC_E_UNKNOWN_CREDENTIALS`) à l'acquisition
des credentials client, avant tout handshake :
`build_native_schannel_probe/ephemeral-result.log` conserve cet ancien échec
code 1. Le code courant retourne désormais **77, SKIP / non qualifié**, pour
ce seul cas à l'acquisition des credentials éphémères ; ce n'est pas un PASS TLS.
Le dernier CTest exécuté dans le sandbox a obtenu **FAIL `0x8009030E`
(`SEC_E_NO_CREDENTIALS`)**. Cette erreur reste un échec, elle n'est pas assimilée
à la précédente. Le parent a réexécuté le CTest courant hors sandbox : le test
est bien marqué **Skipped**, sans preuve TLS éphémère. Il a aussi réexécuté
le mode temporaire avec le même hash de binaire : **PASS, code 0**, trois clés
créées, supprimées et vérifiées absentes. Aucune cause propre au sandbox
n'est encore démontrée.

Ces résultats concernent le profil utilisateur courant. Ils ne prouvent ni les
droits d'accès aux clés sous LocalService, ni l'identité d'un client de pipe,
ni la liaison à une session de logon, ni l'isolation du PCM Windows. Le test
temporaire démontre une possibilité technique pour le canal cryptographique ;
le provisioning et le canal du broker restent à réaliser et qualifier.

## Reproduction du mode éphémère

Depuis la racine vérifiée avec `git rev-parse --show-toplevel` et `git status --short` :

```powershell
cmake -S native-windows-audio/schannel-probe -B build_native_schannel_probe
cmake --build build_native_schannel_probe --config Release
ctest --test-dir build_native_schannel_probe -C Release --output-on-failure
```

Sans argument, les clés ont un nom NULL, ne sont pas persistées et ne sont jamais
exportées. Aucun magasin de certificats ou de confiance global n'est modifié.
Les certificats restent des `CERT_CONTEXT` en mémoire.

## Mode temporaire explicite

Après compilation, cette commande crée des clés dans le profil utilisateur :

```powershell
.\build_native_schannel_probe\Release\NativeAudioSchannelProbe.exe --temporary-user-key
```

Le mode ne s'active jamais automatiquement après un échec éphémère. Les noms
uniques commencent par `TimoxVasioProbe-` et contiennent 128 bits aléatoires.
La politique d'export CNG est fixée à zéro puis relue ; aucun drapeau de clé
machine n'est utilisé, aucun certificat n'est ajouté à un magasin de confiance.
Chaque nom est journalisé avant création. Les sorties normales et les exceptions
déclenchent `NCryptDeleteKey`, puis une tentative `NCryptOpenKey` exacte vérifie
l'absence. Une suppression ou vérification échouée fait échouer le programme.
Un arrêt forcé, crash ou coupure de courant peut empêcher ce nettoyage RAII :
les noms journalisés servent alors à une vérification et suppression ciblées,
jamais à une suppression par joker.

L'enregistrement du test temporaire dans CTest exige également un opt-in :

```powershell
cmake -S native-windows-audio/schannel-probe -B build_native_schannel_probe -DTIMOX_SCHANNEL_TEMPORARY_USER_KEY_TEST=ON
ctest --test-dir build_native_schannel_probe -C Release -R '^NativeAudioSchannelTemporaryUserKey$' --output-on-failure
```

L'option est `OFF` par défaut. La désactiver avec `-DTIMOX_SCHANNEL_TEMPORARY_USER_KEY_TEST=OFF`
retire ce test de CTest ; l'argument explicite de l'exécutable reste disponible.

## Portée des tests

Le programme contient un handshake client/serveur via tokens SSPI relayés en
mémoire. Il exige TLS 1.2 et une suite ECDHE-ECDSA AES-GCM, vérifie le certificat
effectivement négocié contre une empreinte SHA-256 attendue, sa validité et son
EKU. La preuve de possession de clé appartient au handshake Schannel. Le prédicat
local ne remplace pas ce handshake.

Les essais suivants ont réussi dans le mode temporaire :
roundtrip binaire duplex, troncature d'un record, records coalescés et
`SECBUFFER_EXTRA`, record altéré, empreinte serveur/client non autorisée et
absence de certificat client. Le test date/EKU est actuellement un test du
prédicat `Matches`, pas un handshake complet avec un pair expiré/inapproprié.
Le handshake lui-même n'est pas fragmenté par le banc ; seul le chemin de
déchiffrement des records possède ces fixtures. Aucune mesure de performance,
aucun canal de production ou TLS 1.3 n'est qualifié.

Le serveur demande le certificat client avec `ASC_REQ_MUTUAL_AUTH`. Sur cet
hôte, `ISC_REQ_MUTUAL_AUTH` côté client a provoqué `SEC_E_UNSUPPORTED_FUNCTION` ;
ce drapeau n'est donc pas demandé côté client. L'autorisation exige toujours
le certificat distant attendu des deux côtés. L'attribut `RET_INTEGRITY`
n'est pas exigé : le serveur ne le retourne pas dans cet essai. L'intégrité
repose sur la suite AEAD AES-GCM effectivement négociée, avec confidentialité
requise et refus vérifié d'un record altéré. Le protocole est contrôlé via
`SECPKG_ATTR_CONNECTION_INFO`, distinct de la valeur de version filaire
`0x0303` observée dans `SECPKG_ATTR_CIPHER_INFO`.

## Sources Microsoft

- [Clé CNG éphémère avec nom NULL](https://learn.microsoft.com/en-us/windows/win32/api/ncrypt/nf-ncrypt-ncryptcreatepersistedkey).
- [Certificat auto-signé](https://learn.microsoft.com/en-us/windows/win32/api/wincrypt/nf-wincrypt-certcreateselfsigncertificate) et [acquisition du handle privé mis en cache](https://learn.microsoft.com/en-us/windows/win32/api/wincrypt/nf-wincrypt-cryptacquirecertificateprivatekey).
- [Association au fournisseur CNG](https://learn.microsoft.com/en-us/windows/win32/api/wincrypt/ns-wincrypt-crypt_key_prov_info) : `dwKeySpec=0` est le paramètre legacy de `NCryptOpenKey`, distinct de `CERT_NCRYPT_KEY_SPEC` dans le contexte de clé mis en cache.
- [Authentification Schannel et validation du certificat client](https://learn.microsoft.com/en-us/windows/win32/secauthn/performing-authentication-using-schannel).
- [Création du canal](https://learn.microsoft.com/en-us/windows/win32/secauthn/creating-a-secure-connection-using-schannel), [EncryptMessage](https://learn.microsoft.com/en-us/windows/win32/secauthn/encryptmessage--schannel), [DecryptMessage](https://learn.microsoft.com/en-us/windows/win32/secauthn/decryptmessage--schannel).
- [Discussion technique du runtime Microsoft sur les fichiers de clés requis par Schannel](https://github.com/dotnet/runtime/issues/101090) : élément corroborant le résultat observé, pas une qualification de toutes les versions de Windows.
