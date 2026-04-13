/**
 * Cocktail-Automat — app.js
 * MQTT über WebSockets (broker.hivemq.com:8000)
 * Keine Build-Tools – läuft direkt im Browser.
 */

'use strict';

// ─── KONFIGURATION ──────────────────────────────────────────────────────────
const BROKER_WS_URL = 'wss://broker.hivemq.com:8884/mqtt';  // TLS WebSocket

const TOPICS = {
  weight:          'cocktailbot/status/weight',
  relays:          'cocktailbot/status/relays',
  pump:            'cocktailbot/status/pump',
  fluids:          'cocktailbot/status/fluids',
  menu:            'cocktailbot/status/menu',
  recipesOffline:  'cocktailbot/recipes/offline',
  recipesOnline:   'cocktailbot/recipes/online',
  recipeResult:    'cocktailbot/status/recipe_result',
  error:           'cocktailbot/status/error',
};

const FLUID_NAMES = [
  "Vodka","Weisser_Rum","Gin","Tequila","Triple_Sec",
  "Brandy","Whiskey","Dunckler_Rum",
  "Limettensaft","Zitronensaft","Orangensaft","Cranberrysaft",
  "Ananassaft","Grapefruitsaft","Preiselbeersaft","Pfirsichsaft",
  "Zuckersirup","Grenadinesirup","Kokoslikoer","Minzsirup",
  "Kaffeelikoer","Amaretto","Vanillesirup","Ingwersirup",
  "Angostura_Bitter","Soda_Wasser","Tonic_Water","Ginger_Ale",
  "Cola","Wasser","Bitterzitronen_Limonade","Kamille_Tee"
];

const MENU_NAMES = [
  'Hauptmenü','Rezepttyp','Offline Rezepte','Rezept Details',
  'Online Rezepte','Online Rezept Details','Manuelle Eingabe',
  'Mengeneingabe','Flüssigkeiten','Kategorie','Fl. Kategorie',
  'Kalibrierung','Reinigung','Glasgröße','Rezept läuft','Manuelle Steuerung','Motor Schritte'
];

// ─── ZUSTAND ─────────────────────────────────────────────────────────────────
let client = null;
let relayStates = new Array(10).fill(false);  // aktueller Zustand aus MQTT
let configuredFluids = new Array(9).fill(null).map((_, i) => ({ slot: i, name: 'Wasser' }));
let recipes = [];  // Offline-Rezept-Liste (für Name-Lookup im Glasgrößen-Popup)
let onlineRecipesList = [];  // Online-Rezept-Liste (für Re-Render bei Fluid-Änderung)

// ─── MQTT VERBINDUNG ─────────────────────────────────────────────────────────
function connectMQTT() {
  const clientId = 'cocktail_web_' + Math.random().toString(16).slice(2, 8);
  client = mqtt.connect(BROKER_WS_URL, {
    clientId,
    clean: true,
    connectTimeout: 10000,
    reconnectPeriod: 5000,
  });

  client.on('connect', () => {
    setConnStatus(true);
    // Alle Status-Topics subscriben
    Object.values(TOPICS).forEach(t => client.subscribe(t));
    showToast('MQTT verbunden ✓');
  });

  client.on('disconnect', () => setConnStatus(false));
  client.on('error', () => setConnStatus(false));
  client.on('offline', () => setConnStatus(false));

  client.on('message', (topic, payload) => {
    const msg = payload.toString();
    try {
      handleMessage(topic, msg);
    } catch (e) {
      console.warn('MQTT Nachricht Fehler:', e, topic, msg);
    }
  });
}

function setConnStatus(connected) {
  const el = document.getElementById('connStatus');
  el.textContent = connected ? 'Verbunden' : 'Getrennt';
  el.className = 'badge ' + (connected ? 'badge-connected' : 'badge-disconnected');
}

// ─── EINGEHENDE NACHRICHTEN ──────────────────────────────────────────────────
function handleMessage(topic, msg) {
  switch (topic) {
    case TOPICS.weight:
      document.getElementById('weightDisplay').textContent = parseFloat(msg).toFixed(1) + ' ml';
      break;

    case TOPICS.relays: {
      const arr = JSON.parse(msg);
      relayStates = arr;
      renderStatusRelays(arr);
      renderManualRelayBtns(arr);
      break;
    }

    case TOPICS.pump: {
      const d = JSON.parse(msg);
      const el = document.getElementById('pumpDisplay');
      if (d.running)       el.innerHTML = '<span style="color:#f39c12">⚙️ Rezept läuft</span>';
      else if (d.cleaning) el.innerHTML = '<span style="color:#7c6af7">🧹 Reinigung aktiv</span>';
      else                 el.innerHTML = '<span style="color:#4ecca3">✓ Bereit</span>';
      updateGlass(d.progress || 0, d.running, d.cleaning);
      // Glaserkennung
      const gEl = document.getElementById('glassStatus');
      if (gEl) {
        if (d.glass_detected)        gEl.innerHTML = `🥃 <b>Glas erkannt</b> (~${d.glass_weight} g)`;
        else if (d.glass_detecting)  gEl.innerHTML = '⏳ Erkenne Glas...';
        else                         gEl.innerHTML = '<span style="opacity:.5">Kein Glas</span>';
      }
      break;
    }

    case TOPICS.fluids: {
      const arr = JSON.parse(msg);
      configuredFluids = arr;
      renderFluidSlots(arr);
      updateCalibrationDisplay();   // Kalibrierungs-Inputs aktuell halten
      // Online-Rezepte neu rendern damit Kompatibilität aktuell ist
      if (onlineRecipesList.length > 0) renderOnlineRecipes(onlineRecipesList);
      // Offline-Rezepte auch neu rendern
      if (recipes.length > 0) renderOfflineRecipes(recipes);
      break;
    }

    case TOPICS.menu: {
      const d = JSON.parse(msg);
      const name = MENU_NAMES[d.menu] || ('State ' + d.menu);
      document.getElementById('menuDisplay').textContent = name + '  [idx ' + d.selectedIndex + ']';
      break;
    }

    case TOPICS.recipesOffline: {
      recipes = JSON.parse(msg);  // In globalem Array speichern für Glasgrößen-Popup
      renderOfflineRecipes(recipes);
      break;
    }

    case TOPICS.recipesOnline: {
      onlineRecipesList = JSON.parse(msg);
      renderOnlineRecipes(onlineRecipesList);
      break;
    }
    case TOPICS.recipeResult: {
      renderRecipeResult(JSON.parse(msg));
      break;
    }
    case TOPICS.error: {
      const d = JSON.parse(msg);
      showToast('❌ ' + (d.error || 'Fehler'));
      break;
    }
  }
}

// ─── RENDER FUNKTIONEN ───────────────────────────────────────────────────────

/** Glas-SVG animieren: progress 0–100, running=bool, cleaning=bool */
function updateGlass(progress, running, cleaning) {
  const fill    = document.getElementById('glassFill');
  const foam    = document.getElementById('glassFoam');
  const pct     = document.getElementById('glassPercent');
  if (!fill) return;

  // Glas-Inneres: y=8 bis y=145 → Höhe=137 SVG-Einheiten
  const maxH = 137;
  const filledH = (progress / 100) * maxH;
  const topY    = 145 - filledH;

  fill.setAttribute('y', topY);
  fill.setAttribute('height', filledH);

  if (cleaning)             fill.setAttribute('fill', '#3a8ef6');
  else if (progress >= 100) fill.setAttribute('fill', '#4ecca3');
  else                      fill.setAttribute('fill', '#7c6af7');

  if (running && progress > 5) {
    foam.setAttribute('cy', topY + 2);
    foam.setAttribute('opacity', '0.6');
  } else {
    foam.setAttribute('opacity', '0');
  }

  pct.textContent = progress + '%';
}

/** Rezept-Ergebnis anzeigen: Soll vs. Ist pro Zutat */
function renderRecipeResult(d) {
  const card = document.getElementById('recipeResultCard');
  const body = document.getElementById('recipeResultBody');
  if (!card || !body) return;

  let html = `<div class="resultHeader">
    <span class="resultName">${d.name}</span>
    <span class="resultTotal">Gesamt: <b>${d.total_actual} ml</b> / ${d.total_target} ml</span>
  </div>
  <table class="resultTable">
    <thead><tr><th>Zutat</th><th>Soll</th><th>Ist</th><th>Diff</th></tr></thead>
    <tbody>`;

  d.ingredients.forEach(ing => {
    const diff = ing.actual - ing.target;
    const cls  = Math.abs(diff) <= 3 ? 'ok' : (diff < 0 ? 'under' : 'over');
    const sign = diff >= 0 ? '+' : '';
    html += `<tr class="${cls}">
      <td>${ing.name}</td>
      <td>${ing.target} ml</td>
      <td><b>${ing.actual} ml</b></td>
      <td>${sign}${diff} ml</td>
    </tr>`;
  });

  html += '</tbody></table>';
  body.innerHTML = html;
  card.style.display = 'block';
  // Zur Status-Sektion scrollen damit Ergebnis sichtbar ist
  showSection('status');
  card.scrollIntoView({ behavior: 'smooth', block: 'start' });
}

/** Status-Sektion: Relay-Badges */
function renderStatusRelays(states) {
  const grid = document.getElementById('relayGrid');
  grid.innerHTML = '';
  states.forEach((on, i) => {
    const d = document.createElement('div');
    d.className = 'relayPill ' + (on ? 'on' : 'off');
    d.textContent = (i < 9 ? 'R' + (i + 1) : 'Luft') + '\n' + (on ? 'AN' : 'AUS');
    d.style.whiteSpace = 'pre-line';
    grid.appendChild(d);
  });
}

/** Manuelle Steuerung: Relay-Toggle-Buttons */
function renderManualRelayBtns(states) {
  const grid = document.getElementById('manualRelayBtns');
  if (!grid) return;
  grid.innerHTML = '';
  states.forEach((on, i) => {
    const btn = document.createElement('button');
    btn.className = 'relayToggleBtn' + (on ? ' active' : '');
    const label = i < 9
      ? (configuredFluids[i] ? configuredFluids[i].name.replace(/_/g, ' ') : 'Relay ' + (i+1))
      : 'Luft';
    btn.textContent = (on ? '🟢 ' : '🔴 ') + label;
    btn.onclick = () => toggleRelay(i);
    grid.appendChild(btn);
  });
}

/** Offline-Rezepte rendern */
function renderOfflineRecipes(recipes) {
  const list = document.getElementById('offlineRecipeList');
  if (!recipes || recipes.length === 0) {
    list.innerHTML = '<p class="loading">Keine Offline-Rezepte vorhanden.</p>';
    return;
  }
  list.innerHTML = '';
  recipes.forEach((r, idx) => {
    const card = document.createElement('div');
    card.className = 'recipeCard' + (r.compatible ? '' : ' incompatible');

    const info = document.createElement('div');
    info.className = 'recipeInfo';
    info.innerHTML = `
      <div class="recipeName">${r.name}</div>
      <div class="recipeIngredients">${(r.fluid_names || []).map((n, i) =>
        n.replace(/_/g, ' ') + ' ' + r.amounts[i] + 'ml').join(' · ')}</div>
    `;
    card.appendChild(info);

    if (r.compatible) {
      const btn = document.createElement('button');
      btn.className = 'btnPrimary';
      btn.textContent = '▶ Starten';
      btn.onclick = () => startOfflineRecipe(r);
      card.appendChild(btn);
    } else {
      const hint = document.createElement('span');
      hint.className = 'small';
      hint.textContent = '✗ fehlt';
      card.appendChild(hint);
    }
    list.appendChild(card);
  });
}

/** Online-Rezepte rendern */
function renderOnlineRecipes(recipes) {
  const list = document.getElementById('onlineRecipeList');
  if (!recipes || recipes.length === 0) {
    list.innerHTML = '<p class="loading">Keine Online-Rezepte geladen.</p>';
    return;
  }
  // Alle aktuell konfigurierten Flüssigkeitsnamen als Set
  const availableFluids = new Set(configuredFluids.map(f => f.name));

  list.innerHTML = '';
  recipes.forEach((r) => {
    // Prüfen ob alle Zutaten des Rezepts vorhanden sind
    const missing = (r.fluid_names || []).filter(n => !availableFluids.has(n));
    const compatible = missing.length === 0;

    const card = document.createElement('div');
    card.className = 'recipeCard' + (compatible ? '' : ' incompatible');

    const info = document.createElement('div');
    info.className = 'recipeInfo';
    info.innerHTML = `
      <div class="recipeName">${r.name}</div>
      <div class="recipeIngredients">${(r.fluid_names || []).map((n, i) =>
        n.replace(/_/g, ' ') + ' ' + r.amounts[i] + 'ml').join(' · ')}</div>
      ${!compatible ? `<div class="small" style="color:#e74c3c">✗ fehlt: ${missing.map(n => n.replace(/_/g, ' ')).join(', ')}</div>` : ''}
    `;
    card.appendChild(info);

    if (compatible) {
      const btn = document.createElement('button');
      btn.className = 'btnPrimary';
      btn.textContent = '▶ Starten';
      btn.onclick = () => startOnlineRecipe(r);
      card.appendChild(btn);
    } else {
      const hint = document.createElement('span');
      hint.className = 'small';
      hint.textContent = '✗ fehlt';
      card.appendChild(hint);
    }

    list.appendChild(card);
  });
}

/** Flüssigkeits-Slots mit Dropdown rendern */
function renderFluidSlots(fluids) {
  const container = document.getElementById('fluidSlots');
  if (!container) return;
  container.innerHTML = '';
  fluids.forEach((f) => {
    const row = document.createElement('div');
    row.className = 'fluidSlot';

    const lbl = document.createElement('label');
    lbl.textContent = 'Slot ' + (f.slot + 1);

    const sel = document.createElement('select');
    sel.id = 'fluidSelect_' + f.slot;
    FLUID_NAMES.forEach(name => {
      const opt = document.createElement('option');
      opt.value = name;
      opt.textContent = name.replace(/_/g, ' ');
      if (name === f.name) opt.selected = true;
      sel.appendChild(opt);
    });

    row.appendChild(lbl);
    row.appendChild(sel);
    container.appendChild(row);
  });
}

// ─── AKTIONEN ────────────────────────────────────────────────────────────────

/** Offline-Rezept starten */
function startOfflineRecipe(r) {
  const totalMl = r ? (r.amounts || []).reduce((s, a) => s + (a || 0), 0) : 0;
  showGlassSizeModal(r ? r.name : '', totalMl, (glass) => {
    sendCmd('cocktailbot/command/recipe/offline', JSON.stringify({ name: r.name, glass }));
    showToast('Rezept-Start gesendet…');
  });
}

/** Online-Rezept starten – komplettes Recipe-Objekt schicken */
function startOnlineRecipe(recipe) {
  const totalMl = (recipe.amounts || []).reduce((s, a) => s + (a || 0), 0);
  showGlassSizeModal(recipe.name, totalMl, (glass) => {
    const ingredients = (recipe.fluid_names || []).map((name, i) => ({
      fluid: name,
      amount: recipe.amounts[i]
    }));
    sendCmd('cocktailbot/command/recipe/online', JSON.stringify({
      name: recipe.name,
      ingredients,
      glass
    }));
    showToast('Online-Rezept gesendet…');
  });
}

/** Relay an/aus schalten (Toggle) */
function toggleRelay(index) {
  const newState = !relayStates[index];
  sendCmd('cocktailbot/command/relay', JSON.stringify({ index, state: newState }));
}

/** Motor-Command senden */
function motorCmd(action) {
  const steps = parseInt(document.getElementById('motorSteps').value) || 800;
  sendCmd('cocktailbot/command/motor', JSON.stringify({ action, steps }));
  showToast(`Motor ${action === 'forward' ? 'vorwärts' : 'rückwärts'} (${steps} Schritte)`);
}

/** Alle Flüssigkeits-Slots speichern */
function saveAllFluids() {
  let count = 0;
  configuredFluids.forEach((f) => {
    const sel = document.getElementById('fluidSelect_' + f.slot);
    if (sel && sel.value !== f.name) {
      sendCmd('cocktailbot/command/fluid/set', JSON.stringify({ slot: f.slot, name: sel.value }));
      count++;
    }
  });
  showToast(count > 0 ? `${count} Änderung(en) gesendet` : 'Keine Änderungen');
}

/** ESP32 anweisen Online-Rezepte neu zu laden */
function refreshOnlineRecipes() {
  // Leere Nachricht auf einem speziellen Topic – ESP32 reagiert mit fetchOnlineRecipes()
  sendCmd('cocktailbot/command/recipes/refresh', '{}');
  document.getElementById('onlineRecipeList').innerHTML =
    '<p class="loading">ESP32 lädt Rezepte...</p>';
  showToast('Lade-Anfrage gesendet…');
}

/** MQTT-Nachricht senden (allgemein) */
// ─── GLASGRÖSSE MODAL ────────────────────────────────────────────────────────

let _glassSizeCallback = null;

/** Zeigt das Glasgrößen-Popup. cb(glass) wird mit "klein"|"mittel"|"normal"|"gross"|"original" aufgerufen. */
function showGlassSizeModal(recipeName, totalMl, cb) {
  _glassSizeCallback = cb;
  const hint = document.getElementById('glassSizeHint');
  if (hint) hint.textContent = recipeName ? `Rezept: "${recipeName}"` : 'Glasgröße auswählen';
  // Original-Button zeigt die echte Rezeptmenge
  const origSub = document.getElementById('glassSizeOrigSub');
  if (origSub) origSub.textContent = totalMl > 0 ? totalMl + 'ml' : 'exakt';
  document.getElementById('glassSizeModal').classList.remove('hidden');
}

function confirmGlassSize(glass) {
  document.getElementById('glassSizeModal').classList.add('hidden');
  if (_glassSizeCallback) { _glassSizeCallback(glass); _glassSizeCallback = null; }
}

function closeGlassSizeModal() {
  document.getElementById('glassSizeModal').classList.add('hidden');
  _glassSizeCallback = null;
}

// ─── MQTT-SEND HELPER ────────────────────────────────────────────────────────

function sendCmd(topic, payload) {
  if (!client || !client.connected) {
    showToast('❌ Nicht verbunden!');
    return;
  }
  client.publish(topic, payload, { qos: 0 });
}

// ─── KALIBRIERUNG ────────────────────────────────────────────────────────────

/** Zeigt calibration_ml und korrektur_faktor des aktuell gewählten Slots an. */
function updateCalibrationDisplay() {
  const slot = parseInt(document.getElementById('calSlotSelect').value, 10);
  const fluid = configuredFluids[slot];
  if (!fluid) return;
  document.getElementById('calSlotName').textContent = fluid.name ? fluid.name.replace(/_/g, ' ') : '';
  document.getElementById('calFluidMl').value   = fluid.calibration_ml   !== undefined ? fluid.calibration_ml   : 10;
  document.getElementById('calKorrektur').value = fluid.korrektur_faktor !== undefined ? fluid.korrektur_faktor : 10;
}

/** Sendet neue Fluid-Kalibrierungswerte für den gewählten Slot ans ESP32. */
function saveFluidCalibration() {
  const slot     = parseInt(document.getElementById('calSlotSelect').value, 10);
  const cal_ml   = parseFloat(document.getElementById('calFluidMl').value);
  const korrektur = parseInt(document.getElementById('calKorrektur').value, 10);
  if (isNaN(slot) || isNaN(cal_ml) || cal_ml <= 0) {
    showToast('❌ Ungültige Eingabe');
    return;
  }
  const payload = JSON.stringify({ slot, calibration_ml: cal_ml, korrektur });
  sendCmd('cocktailbot/command/calibrate/fluid', payload);
  showToast('✅ Fluid-Kalibrierung Slot ' + (slot + 1) + ' gespeichert');
}

/** Sendet neuen Wägezellen-Kalibrierungsfaktor ans ESP32. */
function saveScaleCalibration() {
  const factor = parseFloat(document.getElementById('scaleFactorInput').value);
  if (isNaN(factor) || factor <= 0) {
    showToast('❌ Ungültiger Faktor');
    return;
  }
  sendCmd('cocktailbot/command/calibrate/scale', JSON.stringify({ factor }));
  document.getElementById('scaleFactorDisplay').textContent = factor;
  showToast('✅ Waage-Faktor auf ' + factor + ' gesetzt');
}

// ─── MANUELLE REZEPT-ERSTELLUNG ──────────────────────────────────────────────

/** Fügt eine neue Zutaten-Zeile hinzu (max. 9) */
function addIngredientRow() {
  const container = document.getElementById('newRecipeRows');
  if (!container) return;
  if (container.querySelectorAll('.ingredientRow').length >= 9) {
    showToast('❌ Maximal 9 Zutaten'); return;
  }
  const row = document.createElement('div');
  row.className = 'ingredientRow';

  const sel = document.createElement('select');
  sel.className = 'ingredientFluid';
  FLUID_NAMES.forEach(name => {
    const opt = document.createElement('option');
    opt.value = name;
    opt.textContent = name.replace(/_/g, ' ');
    sel.appendChild(opt);
  });

  const amt = document.createElement('input');
  amt.type = 'number';
  amt.className = 'ingredientAmount';
  amt.min = '1'; amt.max = '500'; amt.value = '50';

  const del = document.createElement('button');
  del.className = 'btnDel';
  del.textContent = '✕';
  del.onclick = () => row.remove();

  row.append(sel, amt, del);
  container.appendChild(row);
}

/** Sammelt Formular-Daten als Recipe-Objekt – gibt null bei Fehler zurück */
function getManualRecipe() {
  const name = (document.getElementById('newRecipeName').value || '').trim();
  if (!name) return null;
  const rows = document.querySelectorAll('.ingredientRow');
  if (!rows.length) return null;
  const ingredients = [];
  let ok = true;
  rows.forEach(r => {
    const fluid = r.querySelector('.ingredientFluid').value;
    const amount = parseInt(r.querySelector('.ingredientAmount').value, 10);
    if (!fluid || !amount || amount <= 0) { ok = false; return; }
    ingredients.push({ fluid, amount });
  });
  return (ok && ingredients.length) ? { name, ingredients } : null;
}

/** Startet das manuell erstellte Rezept direkt via MQTT (wie Online-Rezept) */
function startManualRecipe() {
  const r = getManualRecipe();
  if (!r) { showToast('❌ Name oder Zutaten fehlen'); return; }
  const totalMl = r.ingredients.reduce((s, i) => s + i.amount, 0);
  showGlassSizeModal(r.name, totalMl, glass => {
    sendCmd('cocktailbot/command/recipe/online',
      JSON.stringify({ name: r.name, ingredients: r.ingredients, glass }));
    showToast('▶ Rezept gestartet: ' + r.name);
  });
}

/** Kopiert das Rezept als JSON in die Zwischenablage */
async function copyRecipeJSON() {
  const r = getManualRecipe();
  if (!r) { showToast('❌ Name oder Zutaten fehlen'); return; }
  try {
    await navigator.clipboard.writeText(JSON.stringify(r, null, 2));
    showToast('📋 JSON kopiert!');
  } catch (_) { showToast('❌ Kopieren fehlgeschlagen'); }
}

/** Lädt GitHub-Einstellungen aus localStorage */
function loadGitHubSettings() {
  document.getElementById('ghToken').value  = localStorage.getItem('cbot_gh_token')  || '';
  document.getElementById('ghRepo').value   = localStorage.getItem('cbot_gh_repo')   || 'GerstnerPhilip/Abschlussprojekt_Gerstner';
  document.getElementById('ghPath').value   = localStorage.getItem('cbot_gh_path')   || 'recipes.json';
  document.getElementById('ghBranch').value = localStorage.getItem('cbot_gh_branch') || 'main';
}

/** Speichert GitHub-Einstellungen in localStorage */
function saveGitHubSettings() {
  localStorage.setItem('cbot_gh_token',  document.getElementById('ghToken').value);
  localStorage.setItem('cbot_gh_repo',   document.getElementById('ghRepo').value);
  localStorage.setItem('cbot_gh_path',   document.getElementById('ghPath').value);
  localStorage.setItem('cbot_gh_branch', document.getElementById('ghBranch').value);
  showToast('✅ GitHub-Einstellungen gespeichert');
}

/** Speichert das Rezept in recipes.json auf GitHub via Contents API */
async function saveRecipeToGitHub() {
  const r = getManualRecipe();
  if (!r) { showToast('❌ Name oder Zutaten fehlen'); return; }
  const token  = document.getElementById('ghToken').value.trim();
  const repo   = document.getElementById('ghRepo').value.trim();
  const path   = document.getElementById('ghPath').value.trim();
  const branch = document.getElementById('ghBranch').value.trim();
  if (!token) { showToast('❌ Kein GitHub-Token gesetzt'); return; }

  const apiUrl = `https://api.github.com/repos/${repo}/contents/${path}`;
  const headers = { 'Authorization': 'Bearer ' + token, 'Accept': 'application/vnd.github.v3+json', 'Content-Type': 'application/json' };

  showToast('☁ Verbinde mit GitHub…');
  try {
    // 1. Aktuelle Datei + SHA holen (404 = Datei existiert noch nicht → neu anlegen)
    let list = [];
    let sha = undefined;
    const getRes = await fetch(`${apiUrl}?ref=${branch}`, { headers });
    if (getRes.ok) {
      const fileInfo = await getRes.json();
      sha = fileInfo.sha;
      try { list = JSON.parse(atob(fileInfo.content.replace(/\n/g, ''))); } catch (_) {}
    } else if (getRes.status !== 404) {
      const err = await getRes.json().catch(() => ({}));
      throw new Error('GET ' + getRes.status + ': ' + (err.message || ''));
    }
    // 404 → Datei existiert nicht, list bleibt [], sha bleibt undefined → Datei wird neu erstellt

    // 2. Neues Rezept anhängen
    list.push(r);

    // 3. Datei hochladen (PUT erstellt neu wenn kein sha, updated wenn sha vorhanden)
    const body = { message: 'Add recipe: ' + r.name, content: btoa(unescape(encodeURIComponent(JSON.stringify(list, null, 2)))), branch };
    if (sha) body.sha = sha;
    const putRes = await fetch(apiUrl, { method: 'PUT', headers, body: JSON.stringify(body) });
    if (!putRes.ok) {
      const err = await putRes.json().catch(() => ({}));
      throw new Error('PUT ' + putRes.status + ': ' + (err.message || ''));
    }
    showToast('✅ "' + r.name + '" auf GitHub gespeichert!');
  } catch (e) {
    showToast('❌ Fehler: ' + e.message);
  }
}

// ─── NAVIGATION ──────────────────────────────────────────────────────────────
function showSection(id) {
  document.querySelectorAll('.section').forEach(s => s.classList.remove('active'));
  document.querySelectorAll('.navBtn').forEach(b => b.classList.remove('active'));
  document.getElementById(id).classList.add('active');
  // Aktive Nav-Schaltfläche markieren
  document.querySelectorAll('.navBtn').forEach(b => {
    if (b.getAttribute('onclick') && b.getAttribute('onclick').includes(id))
      b.classList.add('active');
  });
}

// ─── TOAST ───────────────────────────────────────────────────────────────────
let toastTimer = null;
function showToast(msg) {
  const el = document.getElementById('toast');
  el.textContent = msg;
  el.classList.remove('hidden');
  if (toastTimer) clearTimeout(toastTimer);
  toastTimer = setTimeout(() => el.classList.add('hidden'), 3000);
}

// ─── INIT ─────────────────────────────────────────────────────────────────────
connectMQTT();
// Initiale Relay-Anzeige (leer bis MQTT-Daten ankommen)
renderStatusRelays(new Array(10).fill(false));
renderManualRelayBtns(new Array(10).fill(false));
// Manuelle Rezept-Erstellung: erste Zeile + GitHub-Einstellungen laden
addIngredientRow();
loadGitHubSettings();
