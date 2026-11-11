const path = require('node:path');
const release = process.env.GADGET_COMPANION_RELEASE === '1';
if (release && (!process.env.APPLE_SIGNING_IDENTITY || !process.env.APPLE_API_KEY || !process.env.APPLE_API_KEY_ID || !process.env.APPLE_API_ISSUER)) {
  throw new Error('Public release builds require a Developer ID Application identity and Apple notarization API credentials.');
}
module.exports = {
  packagerConfig: {
    asar: true,
    name: 'Gadget Companion',
    executableName: 'Gadget Companion',
    appBundleId: 'com.thingpulse.gadget-companion',
    appCategoryType: 'public.app-category.utilities',
    icon: path.join(__dirname, 'assets', 'companion'),
    extendInfo: {
      LSUIElement: true,
      NSLocalNetworkUsageDescription: 'Gadget Companion finds your Gadget and sends system stats and optional Codex usage over your local Wi-Fi.',
    },
    ignore: [/^\/test($|\/)/, /^\/out($|\/)/, /^\/node_modules($|\/)/, /^\/site($|\/)/, /^\/desktop\/smoke\.mjs$/, /^\/\.git/],
    ...(release ? {
      osxSign: { identity: process.env.APPLE_SIGNING_IDENTITY },
      osxNotarize: {appleApiKey:process.env.APPLE_API_KEY,appleApiKeyId:process.env.APPLE_API_KEY_ID,appleApiIssuer:process.env.APPLE_API_ISSUER},
    } : {}),
  },
  makers: [
    { name: '@electron-forge/maker-zip', platforms: ['darwin'] },
  ],
};
