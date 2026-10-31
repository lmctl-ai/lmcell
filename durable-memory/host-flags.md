# Host flag cutover

Only --lmcode, --lmcode-schema and --lmcode-signatures select the host language
protocol. The normal gate explicitly refuses all retired spellings before input
file access and compares new schema/signature output with the owner/compiler.
DSLB magic, .dslb paths and cell-model behavior are unchanged.

The SDK is refreshed through sync-sdk.sh at 20325f7. Source compiler builds are
qualified by v6/206-standard capabilities rather than package-version metadata.
The complete Linux gate passes 22 checks: 19 native regression cases, 23 programs,
32 deterministic result files, 24 failure cases, dry-run protection, memory
accounting, implicit std and 20 gallery assets. Rendering dependencies are the
private updated plot/draw hosts. No provider, global install or release was used.
