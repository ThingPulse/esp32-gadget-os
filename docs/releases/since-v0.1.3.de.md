# Gadget OS Neuerungen seit v0.1.3

Neue Apps und Verbesserungen für das **Color Kit Grande**: das Zuhause steuern,
die ISS beobachten, Abfahrten prüfen und Aktien verfolgen – direkt vom Startbildschirm.

Die Screenshots zeigen Beispieldaten.

[English](since-v0.1.3.en.md)

## Home Assistant

**Home Assistant** ist jetzt auf dem Hauptbildschirm verfügbar. Verbinde deine
eigene Home-Assistant-Installation und ordne Lampen, Schalter, Sensoren, Szenen und
Skripte auf bis zu vier eigenen Seiten an. Öffne **Einrichten** und scanne den
QR-Code, um Geräte bequem im Browser auszuwählen und anzuordnen.

![Home-Assistant-Geräte auf einer Seite](images/home-assistant-de.png)

Kompakte Zeilen zeigen mehr vom Gerätenamen und bieten Platz für sieben Einträge
pro Seite. Bei nur einer Seite entfallen die Bedienelemente zum Blättern. Über den
Pfeil einer Lampe lassen sich Helligkeit, Farbtemperatur und – falls unterstützt –
die Farbe ändern. Gerätename und Ein-/Aus-Zustand stehen gemeinsam auf der Taste,
sodass mehr Platz für die Regler bleibt.

![Helligkeit, Farbtemperatur und Farbe](images/home-assistant-color-de.png)

Die Gerätesuche startet mit Lampen, lädt erst nach einer Suche und zeigt jeweils
25 Treffer. Nicht verfügbare Geräte sind standardmässig ausgeblendet; der Filter
lässt sich abschalten. Geräte können auch direkt über ihre Entity-ID hinzugefügt
werden. Ausstehende Aktionen werden angezeigt, bis Home Assistant sie bestätigt.

### Home Assistant per QR-Code und Browser einrichten

Du benötigst eine bestehende Home-Assistant-Installation und ein Smartphone oder
einen Computer im selben vertrauenswürdigen WLAN wie dein Color Kit Grande.
Die Bilder zeigen Beispielgeräte und Demo-Zugangsdaten. Die Browseroberfläche
ist englisch; die folgenden Schritte nennen ihre tatsächlichen Schaltflächen.

**1. QR-Code auf dem Gerät öffnen.** Öffne **Home Assistant** und tippe auf
**Einrichten** beziehungsweise das Zahnrad bei einem bereits eingerichteten
Dashboard. Lass diese Ansicht während der Einrichtung im Browser geöffnet.
Nach zehn Minuten ohne Aktivität wird die Einrichtung geschlossen.

![Einrichtungsansicht auf dem Gerät mit QR-Code](images/home-setup-device-de.png)

**2. Den QR-Code deines Geräts scannen.** Öffne den Link mit der Kamera deines
Smartphones. Die Einrichtungsseite wird direkt vom Gerät geladen und der Browser
automatisch gekoppelt. Scanne den Code auf deinem Gerät, nicht das Beispiel oben.
Am Computer kannst du alternativ die vollständige angezeigte Adresse einschliesslich
`#` und Kopplungscode eingeben. Diese Adresse ist nicht die Adresse deines
Home-Assistant-Servers.

**3. Verbindung zu Home Assistant eintragen.** Gib unter **1 · Connect** die
Serveradresse ein, beispielsweise `http://homeassistant.local:8123`. Öffne in
Home Assistant dein Profil und unter **Security / Sicherheit** den Bereich für
langlebige Zugriffstokens. Erstelle dort ein Token für Gadget OS und kopiere es
sofort; Home Assistant kann es später nicht erneut anzeigen.
[Home-Assistant-Anleitung zu Zugriffstokens](https://contributors.home-assistant.io/core/internals/auth-api/#long-lived-access-tokens).
Füge es auf der Einrichtungsseite unter **Long-lived access token** ein und wähle
**Save connection & continue**. Das Token
wird im Browser verdeckt angezeigt und auf dem Gerät gespeichert; Layout-Exporte
enthalten es nicht.

![Verbindungsformular mit Beispieladresse und verdecktem Demo-Token](images/home-setup-connect.png)

**4. Geräte auswählen.** Wähle die Zielseite und den Gerätetyp und tippe auf
**Search**. Zunächst sind Lampen ausgewählt; **Hide unavailable entities** blendet
nicht verfügbare Geräte standardmässig aus. Tippe neben einem Gerät auf **Add**;
**Added** bestätigt die Auswahl. Ergebnisse erscheinen in Gruppen von 25.
Falls dein Home-Assistant-Konto die Suche nicht verwenden darf, nutze **Add by
entity ID**, zum Beispiel mit `light.living_room`. Die automatische Suche benötigt
ein Administrator-Token. Die manuelle Eingabe erweitert die Berechtigungen des
Kontos nicht.

![Geräteauswahl im Browser mit einer hinzugefügten Lampe](images/home-setup-entities.png)

**5. Seiten anordnen und speichern.** Öffne **Arrange pages**. Benenne die Seiten
und ordne die Kacheln per Ziehen oder mit den Pfeiltasten an. Über **Edit** änderst
du Namen und Symbole. Bis zu vier Seiten mit jeweils sieben Einträgen sind möglich.
Die Seiten sind eigene Gruppen; Räume aus Home Assistant werden nicht automatisch
übernommen. Tippe auf **Save to device** und warte auf die Speicherbestätigung.
Tippe anschliessend auf dem Gerät auf **Done**, um das Dashboard zu verwenden.

![Seiteneditor nach dem Speichern des Dashboards](images/home-setup-pages.png)

Ist die Kopplung abgelaufen, öffne die Einrichtung erneut und scanne den neuen
Code. Deine Änderungen bleiben im Browser erhalten; vor dem Speichern ist die
neue Kopplung nötig. Verwende die Einrichtung nur im vertrauenswürdigen lokalen
Netzwerk. Die Seite nutzt HTTP und sollte nicht aus dem Internet erreichbar sein.

## ISS

Die neue **ISS**-App zeigt, wann du nach der Internationalen Raumstation Ausschau
halten kannst. Sie verwendet den aktiven Wetterstandort oder ersatzweise den
Standort der Flugzeug-App.

![Countdown und Richtungen des nächsten sichtbaren ISS-Überflugs](../../documents/user-guide/screenshots/iss-next.png)

Du siehst den Countdown, Auf- und Untergangsrichtung, die maximale Höhe über dem
Horizont und die voraussichtliche Sichtbarkeitsdauer. Während des Überflugs werden
Fortschritt und verbleibende sichtbare Zeit angezeigt. Die Überflugliste lässt
sich zwischen sichtbaren und allen Überflügen umschalten. Vorhersagen bleiben
gespeichert; ältere Daten und Offline-Zustände sind gekennzeichnet. Freie Sicht
und ein klarer Himmel sind weiterhin nötig. Die Abbildung zeigt die englische Ansicht.

## ÖPNV

Die neue **ÖPNV**-App bringt die Abfahrtstafel auf den Hauptbildschirm. Suche nach
Haltestellen, speichere bis zu acht Favoriten und wische zwischen ihren Tafeln.
Angezeigt werden Ziele, Gleise, Abfahrts-Countdowns sowie Verspätungen und Ausfälle,
soweit der Betreiber diese meldet. Tippe auf eine Abfahrt, um die Zwischenhalte
zu sehen.

![Abfahrtstafel](../../documents/user-guide/screenshots/de/transit.png)

Die Uhrzeiten folgen der Gerätezeitzone. Während die App geöffnet ist, werden
Abfahrten aktualisiert. Bei einem Fehler bleiben die zuletzt geladenen Ergebnisse
sichtbar. Abdeckung und Echtzeitdaten hängen vom Betreiber und Transitous ab.

## Aktien

Die neue **Aktien**-App unterstützt bis zu zehn Symbole, auch von internationalen
Börsen. Öffne einen Eintrag für Kurs, Tagesänderung und Diagramme über fünf Tage,
einen Monat oder ein Jahr. Wische zwischen Aktien oder bearbeite die Liste, um
Symbole hinzuzufügen, zu entfernen und umzuordnen.

| Aktienliste | AAPL-Kurs und Verlauf |
| --- | --- |
| ![Aktienliste](../../documents/user-guide/screenshots/de/stocks.png) | ![AAPL-Detailansicht mit Kurs und Diagramm](images/stocks-detail-de.png) |

Kurse behalten ihre jeweilige Währung. Sie können verzögert sein; gespeicherte
oder nicht verfügbare Daten werden entsprechend gekennzeichnet.

## Flugzeuge

Das Radar bietet mehr Platz für Flugzeuge sowie einen wählbaren Radius und
metrische oder imperiale Einheiten. Wähle ein Flugzeug, um seine Spur und weitere
Flugzeug- und Routendetails zu sehen. Höhenabhängige Farben und verblassende ältere
Spurabschnitte helfen, Steig- und Sinkflüge zu erkennen.

| Radar mit gespeichertem ZRH-Flughafenpunkt | Details eines ausgewählten Flugs |
| --- | --- |
| ![Flugzeugradar mit dezenter ZRH-Markierung](images/planes-radar-de.png) | ![Dialog mit Flugzeug- und Routendetails](images/planes-detail-de.png) |

Nahe Flughäfen erscheinen als dezente Punkte mit Kürzeln, sobald sie aus
Flugrouten bekannt sind. Diese Markierungen bleiben über Neustarts gespeichert.
Ein anderer Standort oder Radius ersetzt den bisherigen Flughafen-Cache.
Eine zusätzliche Flughafendatenbank muss nicht heruntergeladen werden.

## Wetter

Wechsle per horizontalem Wischen oder über die Standortpfeile zwischen gespeicherten
Orten. Der Windpfeil zeigt jetzt die Strömungsrichtung der Luft; die Wetterwerte
sind übersichtlicher ausgerichtet. Der Luftdruck lässt sich in der imperialen
Ansicht in Zoll Quecksilbersäule anzeigen.

Tippe unter den aktuellen Wetterwerten auf **Stunden**, um die stündliche Vorhersage zu öffnen.

| Aktuelles Wetter | Stündliche Vorhersage |
| --- | --- |
| ![Wetter-Hauptansicht](images/weather-main-de.png) | ![Dialog mit stündlicher Vorhersage](images/weather-hourly-de.png) |

## Einstellungen und Tastatur

Unter **Einstellungen → Sprache** steht jetzt Deutsch zur Verfügung, einschliesslich
App-Ansichten, Einstellungen und Update-Dialogen.

Normale Einstellungen werden sofort gespeichert; kurz erscheint **Gespeichert**.
Formulare verwenden **Verbinden** oder **Speichern**. Beim Verlassen eines
bearbeiteten Formulars kannst du speichern, verwerfen oder weiter bearbeiten.
Die Bildschirmhelligkeit wird beim Loslassen des Reglers gespeichert.

Die Bildschirmtastatur hat grössere Tasten und eine Vorschau der gedrückten Taste.
Die WLAN-Suche spart Tipparbeit; Koordinatenfelder nutzen eine Zahlentastatur.

![Grössere Bildschirmtastatur bei der Eingabe eines WLAN-Namens](images/keyboard-de.png)

## Startbildschirm und Software-Updates

Home Assistant ist aus Labs auf den Hauptbildschirm umgezogen. App-Namen können
jetzt zweizeilig angezeigt werden. Auch ÖPNV, Aktien und ISS sind direkt erreichbar.

Vor einem Update öffnet **Was ist neu?** einen QR-Code zu den bebilderten
Versionshinweisen. Nach der Installation erscheint der Code auch in der
Bestätigung. Beide Codes öffnen direkt die englische Seite mit den bebilderten
Neuerungen und einem Link zur deutschen Übersetzung.

Die neuen Dialoge stehen ab der Firmware mit dieser Funktion bereit. Ältere
Firmware zeigt vor der Installation noch ihren bisherigen Dialog. Nach einer
erfolgreichen Prüfung wird ungefähr einmal täglich nach Updates gesucht;
fehlgeschlagene Prüfungen werden erneut versucht.
