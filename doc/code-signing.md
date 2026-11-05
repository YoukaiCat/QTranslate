# Code signing and SmartScreen

Date: 2026-11-05. Status: not implemented; the `v0.9.0`, `v0.9.1` and `v0.10.0` releases are not
signed.

This document records the signing options for the qtranslate installer, the service requirements and
the implementation plan. The technical limitations of selection capture through UI Automation are
described in [`architecture.md`](architecture.md), §4.4 and §9.2.

## 1. What signing gives

An Authenticode signature binds the file to a certificate and lets Windows verify that the file was
not changed after the build. For the user this looks like the publisher name in SmartScreen instead
of "Unknown publisher".

Important:

- the signature does not make the file safe by itself — it confirms only the publisher and integrity;
- Windows trusts only certificates whose chain leads to a root certificate in the Trusted Root store.
  A self-signed certificate is not in that store;
- SmartScreen takes the reputation of the certificate and the publisher into account. A new
  certificate starts with low reputation, so warnings may remain until download statistics
  accumulate;
- the signature must have an RFC 3161 timestamp: without it the signature becomes invalid after the
  certificate expires;
- not only the installer but also `qtranslate.exe` inside the distribution must be signed, otherwise
  the portable version and the installed program remain unsigned.

## 2. Options

### 2.1. Self-signed certificate

Suitable only for pipeline testing and internal builds. Windows does not trust such a certificate,
the user will still see "Unknown publisher"; installing a root certificate into the user's system
for distribution is unacceptable.

Pros: free; `signtool`, the timestamp and the artifact signing order can be verified.
Cons: does not solve SmartScreen, not suitable for public releases.

### 2.2. SignPath Foundation

Free signing for open-source projects. The certificate is issued to the **SignPath Foundation**, so
the publisher in the file properties will be SignPath Foundation, not qtranslate. The private key is
stored in the SignPath HSM, signing is performed through GitHub Actions; every release is manually
approved by an approver.

Requirements that matter for qtranslate:

- a public repository and an **OSI license** for all components; there is currently no `LICENSE` file;
- a code-signing policy on the main page/in the README with the roles and links to the participants;
- MFA for everyone who has access to GitHub and SignPath;
- the **Authors**, **Reviewers**, **Approvers** roles: authors change code, reviewers check external
  PRs, approvers manually approve every signing request;
- the project must already be released and have a verifiable reputation; the request is reviewed
  manually, and approval is not guaranteed;
- for a single maintainer the roles can be combined, but then there is no separation of duties. It is
  better to have a second trusted approver.

This is the preferred free option. Before applying, a license must be chosen: MIT, Apache-2.0 or
GPL-3.0. MIT is usually enough for a small utility, but the choice is up to the project owner.

### 2.3. Azure Artifact Signing

A Microsoft service, formerly **Trusted Signing**. The Basic plan is $9.99/month for 5 000
signatures, Premium is $99.99/month for 100 000. The certificate and the key are in a managed HSM,
and there is integration with GitHub Actions and `signtool`.

Public Trust limitations (per the documentation as of 2026-11):

- organizations — only the USA, Canada, the EU, the UK, Australia, New Zealand, Japan, South Korea,
  Singapore, Switzerland, Norway, Israel;
- individual developers — only the USA and Canada;
- identity validation in the Azure portal is required, usually 1–20 business days;
- Public Trust is not available for Russia.

An Azure signature gives a baseline reputation in SmartScreen but does not guarantee the absence of
warnings for a new publisher.

### 2.4. Commercial OV/EV certificate

A certificate from DigiCert, Sectigo, SSL.com, Certum and other CAs. OV (Organization Validation) is
cheaper and the reputation accumulates gradually; EV (Extended Validation) gives a higher initial
reputation but costs noticeably more. The approximate price is from a few hundred dollars per year.

Since 2023 the private key must be stored on a hardware device (HSM/token) or in a cloud signing
service (DigiCert KeyLocker, SSL.com eSigner, Sectigo). A regular `.pfx` cannot be used in CI. For CI
a cloud HSM and API authorization are chosen.

### 2.5. GitHub artifact attestations

Free GitHub attestations confirm that the artifact was built from the specified workflow and commit.
They are verified with the `gh attestation verify` command. This is useful for supply-chain security
but **does not replace Authenticode**: SmartScreen and "Unknown publisher" do not change because of
it.

## 3. What to sign

1. `bin\qtranslate.exe` — before packaging, so the signature gets into both the ZIP and the installed
   version.
2. `qtranslate-<version>-win64-setup.exe` — after the Inno Setup build.
3. The uninstaller — through `SignedUninstaller=yes` and `SignTool` in the `.iss`. For SignPath this
   requires separate setup, because signing is performed after the build, not inside the Inno
   compiler.
4. `SHA256SUMS.txt` remains a separate release asset but is not advertised. Authenticode is enough
   for the installer; the exe and the installer need signing, not the checksum file.

Example for `signtool` (certificate in the store or an HSM):

```powershell
signtool sign /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 `
    /a build\artifacts\qtranslate-0.9.2-win64-setup.exe
signtool verify /pa /all build\artifacts\qtranslate-0.9.2-win64-setup.exe
```

For Inno Setup:

```ini
SignTool=signtool sign /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 $f
SignedUninstaller=yes
```

For Azure Artifact Signing the `signtool` command uses `/dlib` and a profile metadata file; the key
and the certificate are not exported to the file system.

## 4. CI integration

- All secrets and HSM access are stored in GitHub Actions secrets or in Azure federated authorization;
  the private key is never committed.
- Signing is performed after the build and tests, but before packaging the ZIP and the installer — so
  that the installer already contains a signed exe.
- For SignPath a GitHub Action is used that submits the artifact, waits for approver approval and
  returns the signed file; the workflow must fail if the signature is not obtained.
- After signing, `signtool verify /pa /all` and a timestamp check are mandatory.
- Signing must not change the ZIP content after it is packaged; the order is: build → sign exe →
  package ZIP/installer → sign installer → `SHA256SUMS.txt`.

## 5. Decision and next steps

The recommended path for qtranslate is **SignPath Foundation**:

1. Choose and add an OSI license (`LICENSE`).
2. Add a **Code signing policy** section to the README with the roles, MFA and links to the
   participants.
3. Apply to SignPath Foundation; attach a link to the repository and the releases.
4. After approval, add the signing workflow, sign `qtranslate.exe` and the installer, and verify the
   signature in CI.
5. Update `README`, the active roadmap [`roadmap/qtranslate/plan.md`](roadmap/qtranslate/plan.md),
   [`architecture.md`](architecture.md) and the release notes. The history stays in the archive
   [`roadmap/qtranslate/legacy-plan.md`](roadmap/qtranslate/legacy-plan.md), which is not updated.

If SignPath does not approve the project or a publisher with the owner's name is needed, Azure
Artifact Signing (if the country is available) or a commercial OV/EV certificate remains. Until
signing is implemented, the releases remain unsigned and the SmartScreen warning is considered a
known limitation.
