#include "Defs.h"
#include "p2Log.h"

#include "j1App.h"
#include "j1Movement.h"
#include "j1EntityFactory.h"
#include "Entity.h"
#include "j1Map.h"
#include "j1Render.h"

#include "Brofiler\Brofiler.h"

#include <queue>

j1Movement::j1Movement() {}

j1Movement::~j1Movement() {}

bool j1Movement::Update(float dt) 
{
	bool ret = true;

	DebugDraw();

	return ret;
}

void j1Movement::DebugDraw() const 
{
	for (list<UnitGroup*>::const_iterator group = unitGroups.begin(); group != unitGroups.end(); ++group) {
		for (list<SingleUnit*>::const_iterator unit = (*group)->units.begin(); unit != (*group)->units.end(); ++unit) {

			if ((*unit)->nextTile.x > -1 && (*unit)->nextTile.y > -1) {

				// Raycast a line between the unit and the nextTile
				iPoint offset = { App->map->data.tile_width / 2, App->map->data.tile_height / 2 };
				iPoint nextPos = App->map->MapToWorld((*unit)->nextTile.x, (*unit)->nextTile.y);
				App->render->DrawLine((*unit)->entity->entityInfo.pos.x + offset.x, (*unit)->entity->entityInfo.pos.y + offset.y, nextPos.x + offset.x, nextPos.y + offset.y, 255, 255, 255, 255);
				App->render->DrawCircle(nextPos.x + offset.x, nextPos.y + offset.y, 10, 255, 255, 255, 255);

				// Draw path
				
				for (uint i = 0; i < (*unit)->path.size(); ++i)
				{
					iPoint pos = App->map->MapToWorld((*unit)->path.at(i).x, (*unit)->path.at(i).y);
					SDL_Rect rect = { pos.x, pos.y, App->map->data.tile_width, App->map->data.tile_height };
					App->render->DrawQuad(rect, 0, 255, 0, 50);
				}
			}
		}
	}
}

bool j1Movement::CleanUp()
{
	bool ret = true;

	return ret;
}

UnitGroup* j1Movement::CreateGroupFromList(list<Entity*> entities)
{
	// If an entity from the list belongs to an existing group, delete the entity from the group
	list<Entity*>::const_iterator it = entities.begin();
	UnitGroup* group = nullptr;

	while (it != entities.end()) {

		group = GetGroupByEntity(*it);
		if (group != nullptr) {
			group->units.remove(group->GetUnitByEntity(*it));

			// If the group is empty, delete it
			if (group->GetSize() == 0)
				unitGroups.remove(group);
		}

		it++;
	}

	group = new UnitGroup(entities);
	unitGroups.push_back(group);

	return group;
}

UnitGroup* j1Movement::CreateGroupFromEntity(Entity* entity) 
{
	// If the entity belongs to an existing group, delete the entity from the group
	UnitGroup* group = nullptr;
	group = GetGroupByEntity(entity);
	if (group != nullptr) {

		group->units.remove(group->GetUnitByEntity(entity));

		// If the group is empty, delete it
		if (group->GetSize() == 0)
			unitGroups.remove(group);
	}

	group = new UnitGroup(entity);
	unitGroups.push_back(group);

	return group;
}

UnitGroup* j1Movement::GetLastGroup() const 
{
	return unitGroups.back();
}

UnitGroup* j1Movement::GetGroupByIndex(uint id) const
{
	list<UnitGroup*>::const_iterator it = unitGroups.begin();
	UnitGroup* group = nullptr;

	advance(it, id);
	if (it != unitGroups.end())
		group = *it;

	return group;
}

UnitGroup* j1Movement::GetGroupByEntity(Entity* entity) const
{
	list<UnitGroup*>::const_iterator groups;
	list<SingleUnit*>::const_iterator units;
	UnitGroup* group = nullptr;

	for (groups = unitGroups.begin(); groups != unitGroups.end(); ++groups) {
		for (units = (*groups)->units.begin(); units != (*groups)->units.end(); ++units) {
			if ((*units)->entity == entity) {
				group = *groups;
				break;
			}
		}
	}

	return group;
}

UnitGroup* j1Movement::GetGroupByEntities(list<Entity*> entities) const 
{
	list<Entity*>::const_iterator it;
	list<UnitGroup*>::const_iterator groups;
	list<SingleUnit*>::const_iterator units;
	uint size = 0;

	for (groups = unitGroups.begin(); groups != unitGroups.end(); ++groups) {

		for (units = (*groups)->units.begin(); units != (*groups)->units.end(); ++units) {

			for (it = entities.begin(); it != entities.end(); ++it) {

				if ((*units)->entity == *it)
					size++;
			}
		}

		if (size == entities.size() && size == (*groups)->GetSize())
			return *groups;

		size = 0;
	}

	return nullptr;
}

MovementState j1Movement::MoveEntity(Entity* entity, float dt) const
{
	MovementState ret = MovementState_NoState;

	UnitGroup* g = GetGroupByEntity(entity);

	if (g == nullptr)
		return MovementState_NoState;

	SingleUnit* u = g->GetUnitByEntity(entity);

	if (u == nullptr)
		return MovementState_NoState;

	ret = u->movementState;

	u->currTile = App->map->WorldToMap(u->entity->entityInfo.pos.x, u->entity->entityInfo.pos.y); // unit current pos in map coords
	iPoint nextPos = App->map->MapToWorld(u->nextTile.x, u->nextTile.y); // unit nextPos in map coords
	
	fPoint movePos;
	fPoint endPos;
	iPoint newTile;

	float m;
	bool increaseWaypoint = false;

	/// For each step:
	switch (u->movementState) {

	case MovementState_WaitForPath:

		// If there is a valid goal:
		if (u->goal.x != -1 && u->goal.y != -1) {

			// Find a path
			if (App->pathfinding->CreatePath(u->currTile, u->goal, DISTANCE_MANHATTAN) == -1)
				break;

			// Save the path found
			u->path = *App->pathfinding->GetLastPath();

			// Set state to IncreaseWaypoint, in order to start following the path
			u->movementState = MovementState_IncreaseWaypoint;
		}

		break;

	case MovementState_FollowPath:

		// MOVEMENT CALCULATION

		// Calculate the difference between nextTile and currTile. The result will be in the interval [-1,1]
		movePos = { (float)nextPos.x - u->entity->entityInfo.pos.x, (float)nextPos.y - u->entity->entityInfo.pos.y };

		// Normalize
		m = sqrtf(pow(movePos.x, 2.0f) + pow(movePos.y, 2.0f));

		if (m > 0.0f) {
			movePos.x /= m;
			movePos.y /= m;
		}

		u->entity->entityInfo.direction.x = movePos.x;
		u->entity->entityInfo.direction.y = movePos.y;

		// Apply the speed and the dt to the previous result
		movePos.x *= u->speed * dt;
		movePos.y *= u->speed * dt;

		// COLLISION CALCULATION

		// Predict where the unit will be after moving
		endPos = { u->entity->entityInfo.pos.x + movePos.x,u->entity->entityInfo.pos.y + movePos.y };

		// If there would be a collision if the unit moved to the endPos:
		if (CheckForFutureCollision(u)) {

			// Find a new, valid nextTile to move
			newTile = FindNewValidTile(u);

			if (newTile.x != -1 && newTile.y != -1) {

				// If the nextTile was going to be the goal tile, stop the unit when it reaches its new nextTile
				if (u->nextTile == u->goal) {

					u->path.clear();
					u->nextTile = newTile;
					u->goal = u->newGoal = u->nextTile;

					break;
				}

				// Update the unit's nextTile
				u->nextTile = newTile;

				// Recalculate the path
				if (App->pathfinding->CreatePath(u->nextTile, u->goal, DISTANCE_MANHATTAN) == -1)
					break;

				// Save the path found
				u->path = *App->pathfinding->GetLastPath();
			}

			break;
		}

		// Check if the unit would reach the nextTile during this move
		/// We check with an offset, in order to avoid the unit to miss the nextTile

		if (u->entity->entityInfo.direction.x >= 0) { // Right or stop

			if (u->entity->entityInfo.direction.y >= 0) { // Down or stop
				if (endPos.x >= nextPos.x && endPos.y >= nextPos.y)
					increaseWaypoint = true;
			}
			else { // Up
				if (endPos.x >= nextPos.x && endPos.y <= nextPos.y)
					increaseWaypoint = true;
			}
		}
		else { // Left

			if (u->entity->entityInfo.direction.y >= 0) { // Down or stop
				if (endPos.x <= nextPos.x && endPos.y >= nextPos.y)
					increaseWaypoint = true;
			}
			else { // Up
				if (endPos.x <= nextPos.x && endPos.y <= nextPos.y)
					increaseWaypoint = true;
			}
		}

		// If the unit's going to reach the nextTile during this move:
		if (increaseWaypoint) {
			u->entity->entityInfo.pos.x = nextPos.x;
			u->entity->entityInfo.pos.y = nextPos.y;
			u->movementState = MovementState_IncreaseWaypoint;
			break;
		}

		// Do the actual move
		u->entity->entityInfo.pos.x += movePos.x;
		u->entity->entityInfo.pos.y += movePos.y;

		break;

	case MovementState_GoalReached:

		// Make the appropiate notifications

		// The unit is still
		u->entity->entityInfo.direction.x = 0.0f;
		u->entity->entityInfo.direction.y = 0.0f;

		// If the goal has been changed:
		if (u->goal != u->newGoal) {
			u->goal = u->newGoal;
			u->movementState = MovementState_WaitForPath;
			break;
		}

		LOG("GOAL REACHED!");

		break;

	case MovementState_CollisionFound:

		// MYTODO: organize the collision code here?

		u->movementState = MovementState_FollowPath;

		break;

	case MovementState_IncreaseWaypoint:

		// If the goal has been changed:
		/// We only want to update the goal when the unit has reached nextTile. That's why we do it here
		if (u->goal != u->newGoal) {
			u->goal = u->newGoal;
			u->movementState = MovementState_WaitForPath;
			break;
		}

		// If the unit's path contains waypoints:
		if (u->path.size() > 0) {

			// Get the next waypoint to head to
			u->nextTile = u->path.front();
			u->path.erase(u->path.begin());

			u->movementState = MovementState_FollowPath;
		}
		else
			// If the unit's path is out of waypoints, it means that the unit has reached the goal
			u->movementState = MovementState_GoalReached;

		break;
	}

	return ret;
}

bool j1Movement::CheckForFutureCollision(SingleUnit* unit) const
{
	/// We don't check the walkability of the tile since the A* algorithm already did it for us

	list<UnitGroup*>::const_iterator groups;
	list<SingleUnit*>::const_iterator units;

	for (groups = unitGroups.begin(); groups != unitGroups.end(); ++groups) {
		for (units = (*groups)->units.begin(); units != (*groups)->units.end(); ++units) {

			if ((*units) != unit) {

				if (unit->nextTile == (*units)->currTile) {
					// A reaches B's tile
					return true;
				}
				else if (unit->nextTile == (*units)->nextTile) {
					// A and B reach the same tile
					return true;
				}
			}
		}
	}

	return false;
}

bool j1Movement::IsValidTile(iPoint tile) const 
{
	list<UnitGroup*>::const_iterator groups;
	list<SingleUnit*>::const_iterator units;

	for (groups = unitGroups.begin(); groups != unitGroups.end(); ++groups) {
		for (units = (*groups)->units.begin(); units != (*groups)->units.end(); ++units) {

			if ((*units)->currTile == tile || (*units)->nextTile == tile)
				return false;
		}
	}

	return true;
}

iPoint j1Movement::FindNewValidTile(SingleUnit* unit) const
{
	// 1. Units can only move in 8 directions from their current tile
	iPoint neighbors[8];
	neighbors[0].create(unit->currTile.x + 1, unit->currTile.y + 0);
	neighbors[1].create(unit->currTile.x + 0, unit->currTile.y + 1);
	neighbors[2].create(unit->currTile.x - 1, unit->currTile.y + 0);
	neighbors[3].create(unit->currTile.x + 0, unit->currTile.y - 1);
	neighbors[4].create(unit->currTile.x + 1, unit->currTile.y + 1);
	neighbors[5].create(unit->currTile.x + 1, unit->currTile.y - 1);
	neighbors[6].create(unit->currTile.x - 1, unit->currTile.y + 1);
	neighbors[7].create(unit->currTile.x - 1, unit->currTile.y - 1);

	// 2. PRIORITY: the neighbor closer to the unit's goal
	priority_queue<iPointPriority, vector<iPointPriority>, Comparator> queue;
	iPointPriority priorityNeighbors;

	for (uint i = 0; i < 8; ++i)
	{
		priorityNeighbors.point = neighbors[i];
		priorityNeighbors.priority = neighbors[i].DistanceManhattan(unit->goal);
		queue.push(priorityNeighbors);
	}
	
	iPointPriority curr;
	while (queue.size() > 0) {

		curr = queue.top();
		queue.pop();

		if (App->pathfinding->IsWalkable(curr.point) && IsValidTile(curr.point))
			return curr.point;
	}

	return { -1,-1 };
}

// UnitGroup struct ---------------------------------------------------------------------------------

UnitGroup::UnitGroup(Entity* entity) 
{
	AddUnit(entity);
}

UnitGroup::UnitGroup(list<Entity*> entities)
{
	list<Entity*>::const_iterator it = entities.begin();

	while (it != entities.end()) {

		AddUnit(*it);
		it++;
	}
}

SingleUnit* UnitGroup::AddUnit(Entity* entity)
{
	// If an the entity belongs to an existing unit, first delete the unit
	SingleUnit* unit = GetUnitByEntity(entity);

	if (unit != nullptr)
		units.remove(unit);
	unit = nullptr;

	unit = new SingleUnit(entity, this);
	units.push_back(unit);

	return unit;
}

bool UnitGroup::RemoveUnit(Entity* entity)
{
	bool ret = false;

	list<SingleUnit*>::const_iterator it = units.begin();

	while (it != units.end()) {

		if ((*it)->entity == entity) {
			units.remove(*it);
			ret = true;
		}
		it++;
	}

	return ret;
}

uint UnitGroup::GetSize() const
{
	return units.size();
}

SingleUnit* UnitGroup::GetUnitByIndex(uint id)
{
	list<SingleUnit*>::const_iterator it = units.begin();

	advance(it, id);
	if (it != units.end())
		return *it;

	return nullptr;
}

SingleUnit* UnitGroup::GetUnitByEntity(Entity* entity) const
{
	list<SingleUnit*>::const_iterator it = units.begin();

	while (it != units.end()) {
		if ((*it)->entity == entity)
			return *it;

		it++;
	}

	return nullptr;
}

bool UnitGroup::SetGoal(iPoint goal)
{
	bool ret = false;

	if (App->pathfinding->IsWalkable(goal)) {

		this->goal = goal;

		// Update all units' newGoal
		list<SingleUnit*>::const_iterator it = units.begin();

		while (it != units.end()) {
			(*it)->newGoal = goal;

			// If the unit didn't have a goal defined, update also its goal
			if ((*it)->goal.x == -1 && (*it)->goal.y == -1)
				(*it)->goal = goal;

			it++;
		}

		LOG("New goal!");

		ret = true;
	}	

	return ret;
}

iPoint UnitGroup::GetGoal() const
{
	return goal;
}

float UnitGroup::GetMaxSpeed() const
{
	return maxSpeed;
}

// Unit struct ---------------------------------------------------------------------------------

SingleUnit::SingleUnit(Entity* entity, UnitGroup* group) :entity(entity), group(group)
{
	currTile = App->map->WorldToMap(entity->entityInfo.pos.x, entity->entityInfo.pos.y);
	speed = entity->entityInfo.speed;
	speed *= 50.0f; // MYTODO: delete this 50.0f magic number!!!

	goal = group->goal;
}