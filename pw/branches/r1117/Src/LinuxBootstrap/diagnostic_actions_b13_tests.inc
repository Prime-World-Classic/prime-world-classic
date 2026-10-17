// Include after B9/B10 and base-attack fixtures. The harness includes Core/Scheduler.h,
// PFAIController.h and PFAIStates.h at global scope, before its anonymous namespace.
namespace diagnostic_actions_b13
{
/** Observe real command submission without executing orders or replacing controller decisions. */
class CommandSink : public NCore::ITransceiver, public BaseObjectST
{
	NI_DECLARE_REFCOUNT_CLASS_2(CommandSink, NCore::ITransceiver, BaseObjectST)
public:
	std::vector<CObj<NCore::WorldCommand>> commands;
	NCore::IWorldBase* world = nullptr;
	void Reinit(NCore::ICommandScheduler*) override {}
	void Step(float) override {}
	void SendCommand(NCore::WorldCommand* command, bool) override { commands.emplace_back(command); }
	void SetWorld(NCore::IWorldBase* value) override { world = value; }
	NCore::IWorldBase* GetWorld() override { return world; }
	void RecordMapStart(const NCore::MapStartInfo&) override {}
	bool IsPaused() const override { return false; }
	bool IsAsynced() const override { return false; }
	int GetNextStep() const override { return 0; }
	void SetNextStep(int) override {}
	void SetPrecalcCrcOnce(bool) override {}
	bool GetNoData() const override { return false; }
	int GetWorldStep() const override { return world ? world->GetStepNumber() : -1; }
	int GetBufferLimit() const override { return 1; }
};

/** Empty real world with scoped bot data; no global SessionRoot replacement or wall-clock sleeps.
 * Controllers are stepped explicitly, outside the empty AI container. Only command delivery is
 * mocked, so health/gold observations cannot be confused with command execution, salary or regen.
 */
struct Fixture
{
	CObj<NCore::IWorldBase> owner;
	PFWorld* world = nullptr;
	NDb::Ptr<NDb::BotsSettings> bots;
	NDb::Ptr<NDb::Consumable> potion;
	CObj<B10CommandHero> hero;
	CObj<PFShop> shop;
	CObj<PFTower> tower;
	CObj<B9CastHero> enemy;
	Strong<NCore::ITransceiver> transport;
	CommandSink* sink = nullptr;
	CObj<PFAIController> controller;

	explicit Fixture(Checks& check)
	{
		auto* botData = new NDb::BotsSettings; bots = botData;
		botData->enableBotsAI = true; botData->midOnly = false;
		botData->minShoppingMoney = 100;
		auto* settings = new NDb::AdvMapSettings; settings->overrideBotsSettings = botData;
		auto* terrain = new NDb::Terrain;
		terrain->elemXCount = 4; terrain->elemYCount = 3; terrain->tilesPerElement = 4;
		auto* map = new NDb::AdvMap; map->terrain = terrain; map->mapSettings = settings;
		auto* description = new NDb::AdvMapDescription; description->map = map;
		NDb::Ptr<NDb::AdvMapDescription> db = description;
		owner = PFWorld::CreatePFWorld(); world = dynamic_cast<PFWorld*>(owner.GetPtr());
		const bool loaded = world && world->LoadMap(db, 0, NCore::TPlayersStartInfo(), 0,
			false, PFResourcesCollection::TalentMap());
		check(loaded, "diagnostic actions: real empty map loads with scoped bot settings");
		if (!loaded) { world = nullptr; return; }
		check(!world->IsLinuxDiagnosticActionsEnabled(), "diagnostic actions: new world defaults to disabled");
		AdvanceTo(760);
		check(world->GetStepNumber() == 760 && world->GetBotsSettings() == bots.GetPtr(),
			"diagnostic actions: simulation reaches proof thresholds without sleeping or changing the clock");
		hero = CommandHeroB10(world);
		hero->SetHealth(hero->GetMaxHealth());
		auto* attack = B11AttackDescriptor(0); attack->useRange.sString = "3";
		hero->ReplaceBaseAttack(new PFBaseAttackData(hero.GetPtr(), attack), false);
		auto* portal = new NDb::Talent; portal->naftaCost = 0; portal->manaCost.sString = "0";
		portal->cooldownTime.sString = "0"; portal->targetType = NDb::SPELLTARGET_LAND;
		hero->InstallPortal(portal);
		// As in ClonedPtr, the registered DB factory marks in-memory descriptors loaded.
		// A manually assigned nonempty DBID would instead trigger lazy filesystem loading.
		auto* potionData = dynamic_cast<NDb::Consumable*>(NDb::GetDbTypesRegistrator().Create(
			NDb::DBID("/B13/HealingPotion.ARCT.xdb"), NDb::Consumable::GetTypeName(), true));
		potion = potionData;
		check(potionData && !potionData->NeedReload(),
			"diagnostic actions: registered factory creates a loaded in-memory healing descriptor");
		if (!potionData) { world = nullptr; return; }
		potionData->maxQuantity = 5; potionData->naftaCost = 1;
		potionData->manaCost.sString = "0"; potionData->cooldownTime.sString = "0";
		check(hero->TakeConsumable(potion, 1, NDb::CONSUMABLEORIGIN_DEFAULT, 0),
			"diagnostic actions: real inventory admits the recognized healing item");
		auto* stock = new NDb::ConsumablesShop; stock->interactionRadius = 10;
		stock->items.push_back(potion);
		auto* shopData = new NDb::Shop; shopData->shop = stock; shopData->faction = NDb::FACTION_FREEZE;
		shopData->stats = hero->DbUnitDesc()->stats; shopData->targetingParams = hero->DbUnitDesc()->targetingParams;
		NDb::AdvMapObject object; object.gameObject = shopData; object.lockMap = false;
		object.offset = CPlacement(CVec3(5, 7, 0), QNULL, CVec3(1, 1, 1));
		shop = new PFShop(world, object);
		world->ProcessAddRemove();
		sink = new CommandSink; sink->world = world; transport = sink;
		controller = new PFAIController(hero, transport, NDb::ROUTE_TOP);
		// Skip only the initial respawn-to-shop transition, not any Step implementation.
		controller->SetDead(false);
		check(hero->GetGold() < botData->minShoppingMoney && hero->GetPortal()
			&& IdentifyConsumable(potion) == OBJECT_HEALING_POTION && shop->IsHeroNear(hero),
			"diagnostic actions: low-gold healthy hero, portal and nearby shop satisfy proof preconditions");
	}

	~Fixture()
	{
		if (controller) controller->Cleanup();
		if (sink) sink->commands.clear();
		if (enemy) enemy->Cleanup();
		if (tower) tower->Cleanup();
		if (shop) shop->Cleanup();
		if (hero) hero->Cleanup();
	}
	void AdvanceTo(int step)
	{
		while (world->GetStepNumber() < step) world->Step(.1f, .1f);
	}
	void Steps(int count = 1)
	{
		for (int i = 0; i < count; ++i) controller->Step(.1f);
	}
	void AddDistantTower()
	{
		auto* db = new NDb::Tower; db->faction = NDb::FACTION_BURN;
		db->routeID = NDb::ROUTE_TOP; db->routeLevel = 0;
		db->stats = hero->DbUnitDesc()->stats; db->targetingParams = hero->DbUnitDesc()->targetingParams;
		NDb::AdvMapObject object; object.gameObject = db; object.lockMap = false;
		object.offset = CPlacement(CVec3(32, 22, 0), QNULL, CVec3(1, 1, 1));
		tower = new PFTower(world, object); world->ProcessAddRemove();
	}
};

/** Run once in a fresh probe process: legacy proof completion flags are intentionally not reset.
 * The default-off checks precede opt-in, so global one-shot completion cannot hide the regression.
 * This covers policy admission and real queued command identity, not scheduler execution/replay.
 */
static void Isolation(Checks& check)
{
	Fixture f(check), other(check); if (!f.world || !other.world) return;
	const int gold = f.hero->GetGold();
	const float health = f.hero->GetHealth();
	f.Steps(2);
	check(f.hero->GetGold() == gold, "diagnostic actions: default-disabled bot receives no proof gold");
	check(f.hero->GetHealth() == health, "diagnostic actions: default-disabled bot receives no proof health rewrite");
	check(f.sink->commands.empty() && f.world->GetLinuxAICommandAttempts() == 0
		&& !f.controller->CurrentState(), "diagnostic actions: default-disabled bot receives no proof orders");

	f.world->SetLinuxDiagnosticActionsEnabled(true);
	check(f.world->IsLinuxDiagnosticActionsEnabled() && !other.world->IsLinuxDiagnosticActionsEnabled(),
		"diagnostic actions: opt-in belongs to one world, not the process");
	f.Steps();
	check(f.hero->GetGold() >= f.bots->minShoppingMoney && f.hero->GetHealth() == health
		&& f.world->GetLinuxAICommandBuyConsumableSent() == 1,
		"diagnostic actions: opt-in retains proof grant and original buy-command submission");
	const size_t pendingBuy = f.sink->commands.size();
	f.world->SetLinuxDiagnosticActionsEnabled(false);
	f.Steps(2);
	check(f.hero->GetHealth() == health && f.sink->commands.size() == pendingBuy
		&& f.world->GetLinuxAICommandUseConsumableSent() == 0,
		"diagnostic actions: disabling preserves queued work but cannot advance pending consumable proof");
	const int otherGold = other.hero->GetGold();
	const float otherHealth = other.hero->GetHealth();
	other.Steps(2);
	check(other.hero->GetGold() == otherGold && other.hero->GetHealth() == otherHealth
		&& other.sink->commands.empty(), "diagnostic actions: another disabled world stays untouched by opt-in");

	f.world->SetLinuxDiagnosticActionsEnabled(true); f.Steps();
	check(f.world->GetLinuxAICommandUseConsumableSent() == 1
		&& fabs(f.hero->GetHealth() - f.hero->GetMaxHealth() * .4f) < .01f,
		"diagnostic actions: re-enabled consumable proof resumes once and retains its explicit health setup");
	// Remove that deliberate damage before testing interaction policy, so ordinary healing is not triggered.
	f.hero->SetHealth(health);
	f.world->SetLinuxDiagnosticActionsEnabled(false);
	const size_t beforeInteraction = f.sink->commands.size(); f.Steps(2);
	check(f.sink->commands.size() == beforeInteraction && f.world->GetLinuxAICommandUsePortalSent() == 0,
		"diagnostic actions: disabled interaction proof cannot issue its portal order");
	f.world->SetLinuxDiagnosticActionsEnabled(true); f.Steps();
	check(f.world->GetLinuxAICommandUsePortalSent() == 1,
		"diagnostic actions: explicit interaction opt-in submits the original portal command");
	f.world->SetLinuxDiagnosticActionsEnabled(false);
	const size_t waiting = f.sink->commands.size(); f.Steps(20);
	check(f.sink->commands.size() == waiting, "diagnostic actions: disabled interaction wait cannot inject commands");
	f.world->SetLinuxDiagnosticActionsEnabled(true); f.Steps(12);
	check(f.sink->commands.size() == waiting && f.world->GetLinuxAICommandUsePortalSent() == 1,
		"diagnostic actions: disabled ticks did not consume the interaction proof delay");
	// Complete the no-flag/no-pickup phases below the tower threshold, without fabricating endpoints.
	f.Steps(14);
	f.world->SetLinuxDiagnosticActionsEnabled(false);
	f.AdvanceTo(850); f.AddDistantTower();
	const size_t beforeTower = f.sink->commands.size(); f.Steps(3);
	check(f.world->FindLinuxUnitByObjectId(f.tower->GetObjectId()) == f.tower.GetPtr()
		&& fabs(f.tower->GetPosition().AsVec2D() - f.hero->GetPosition().AsVec2D()) > f.hero->GetVisibilityRange(),
		"diagnostic actions: real registered route tower is beyond ordinary local acquisition range");
	check(f.sink->commands.size() == beforeTower && !f.controller->CurrentState(),
		"diagnostic actions: disabled tower proof cannot create a remote combat-move state");
	const int combatMoves = f.world->GetLinuxAICommandCombatMoveSent();
	// PushState occurs after FsmStep; OnEnter submits the command on the next controller tick.
	f.world->SetLinuxDiagnosticActionsEnabled(true); f.Steps(2);
	check(f.controller->CurrentState() && f.controller->CurrentState()->stateType == ATTACKINGTOWER
		&& f.world->GetLinuxAICommandCombatMoveSent() == combatMoves + 1,
		"diagnostic actions: explicit tower opt-in retains the original combat-move state and command");

	other.enemy = B9MaleHero(other.world, NDb::FACTION_BURN);
	other.enemy->TeleportTo(CVec2(8, 5), false, false); other.enemy->EnemyVisible(true);
	other.world->ProcessAddRemove();
	// Fog sources are dirty until visibility is flushed; the stealth flag alone does not reveal terrain.
	other.world->Step(0.f, 0.f);
	check(fabs(other.enemy->GetPosition().AsVec2D() - other.hero->GetPosition().AsVec2D())
		< other.hero->GetVisibilityRange() && other.enemy->IsVisibleForEnemy(other.hero->GetFaction()),
		"diagnostic actions: nearby enemy is inside actual vision range and faction fog visibility");
	check(other.controller->GetHelper().FindEnemyNear() == other.enemy.GetPtr(),
		"diagnostic actions: ordinary AI can acquire a real visible nearby enemy");
	const int attacks = other.world->GetLinuxAICommandAttackSent(); other.Steps(5);
	check(!other.world->IsLinuxDiagnosticActionsEnabled()
		&& other.world->GetLinuxAICommandAttackSent() > attacks && !other.sink->commands.empty(),
		"diagnostic actions: disabled proof policy still lets ordinary Step submit attack commands");
	check(f.world->GetLinuxAICommandDirectFallbacks() == 0 && other.world->GetLinuxAICommandDirectFallbacks() == 0,
		"diagnostic actions: observed orders use the transceiver, never direct hero fallback");
}
} // namespace diagnostic_actions_b13

/** Focused fresh-process policy regression; no shipped-data setup or wall-clock delay is needed. */
static void DiagnosticActionsB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	diagnostic_actions_b13::Isolation(check);
	std::printf("B13 diagnostic actions: %d checks, %d failures\n", check.count - before, check.failed - failed);
}
