// Copy this file to "config.js" and fill in your Firebase web config.
// config.js is gitignored and must never be committed.
//
// These web config values are not "secret" in the cryptographic sense (they are
// shipped to the browser), but access control MUST be enforced by Firebase
// Realtime Database security rules + Authentication. See database.rules.json
// and docs/ROADMAP.md (Phase 1).
const firebaseConfig = {
    apiKey: "YOUR_API_KEY",
    authDomain: "YOUR_PROJECT.firebaseapp.com",
    databaseURL: "https://YOUR_PROJECT-default-rtdb.REGION.firebasedatabase.app",
    projectId: "YOUR_PROJECT",
    storageBucket: "YOUR_PROJECT.firebasestorage.app",
    messagingSenderId: "YOUR_SENDER_ID",
    appId: "YOUR_APP_ID"
};
firebase.initializeApp(firebaseConfig);
