from __future__ import unicode_literals

import itertools
import json
import math

from datetime import datetime

import matplotlib
import matplotlib.pyplot as plt
import statistics

import numpy
from numpy import zeros

import multiprocessing
import time
from concurrent.futures import ProcessPoolExecutor, as_completed
import traceback
import sys

from pyinstrument import Profiler
from sklearn.metrics import roc_auc_score
import random


profiler = Profiler()

# [default, step]
RATING_GAIN = 10.0
TEAM_RATING_MOD = 0.004 #0.0027
RATING_POWER = 0.3
SOFT_RATING_POWER = 0.025
BOTTOM_RATING = 0
TOP_RATING = 300

global_params = \
    {
        #'RATING_GAIN': [RATING_GAIN, 5.0],
        #'TEAM_RATING_MOD': [TEAM_RATING_MOD, 0.001],
        #'RATING_POWER': [RATING_POWER, 0.1],
        #'SOFT_RATING_POWER': [SOFT_RATING_POWER, 0.1],
        #'BOTTOM_RATING': [BOTTOM_RATING, 100.0],
        #'TOP_RATING': [TOP_RATING, 100.0]
    }


optimizing_params = ['RATING_POWER', 'TEAM_RATING_MOD']

SIM_STEPS = 2
EPOCHS = 1
STEPS = 7

LAMBDA_AUC = 0.5
LAMBDA_ORIG_WR = 0.0
LAMBDA_NEW_MEAN = 0.0
LAMBDA_NEW_STD = 0.00
LAMBDA_NEW_WR = 0.0

SANITY_PERCENTILE = 2
MIN_RATING_GAIN = 0.0
DEFAULT_RATING = 1500.0
DEFAULT_RATING_MULT = 1.0 #25
RATING_BUCKET = 50.0 / DEFAULT_RATING_MULT
TEAM_SIZE = 5

descent = False
party = True # not descent
findMain = False
profile_enabled = False
change_sim1_rating = False
use_sigmoid = False
diff_buckets = 50
games_with_team_ratings = 0
random.seed(4221)
combinations = {}

with open('game_session.json', 'r') as f:
    GAME_SESSION = json.load(f)[2]['data']
with open('game_session_user.json', 'r') as f:
    GAME_SESSION_USER = json.load(f)[2]['data']
with open('user.json', 'r', encoding='utf-8') as f:
    USERS = json.load(f)[2]['data']
with open('user_mac_address.json', 'r') as f:
    USER_MACS = json.load(f)[2]['data']


class SimulationData:
    def __init__(self):
        self.users_2_macs = {}
        self.macs_2_users = {}
        self.games = {}
        self.user_grating = {}
        self.user_games = {}  # Добавляем для хранения игр по пользователям
        self.sorted_game_times = []  # Для быстрого поиска следующих игр
        self.games_with_parties = {}

        self.sanity = []
        self.auc_wo = []
        self.possible_sanity_abs_mean = []
        self.possible_sanity_std = []
        self.original_outcome_wr = []
        self.possible_outcome_wr = []

        self.player_ratings = []
        self.added_games = [[], []]

        self.original_diffs = [[], []]
        self.possible_diffs = [[], []]

        self.original_wrs = [[], []]
        self.possible_wrs = [[], []]

        self.original_team_diff_mod = [[], []]
        self.possible_team_diff_mod = [[], []]

        # Processing data
        self.outcomes = [[], []]

        self.original_predictions = [[], []]

    def init(self):
        self._init_mac_mappings()
        self._init_games()
        self._calculate_game_durations()  # Добавляем расчет длительности игр
        self._filter_games()

    def _init_mac_mappings(self):
        for usrmac in USER_MACS:
            mac = usrmac['mac']
            usid = usrmac['userId']
            if mac not in self.macs_2_users:
                self.macs_2_users[mac] = []
            if usid not in self.users_2_macs:
                self.users_2_macs[usid] = []
            self.macs_2_users[mac].append(usid)
            self.users_2_macs[usid].append(mac)

    def _init_games(self):
        for u in USERS:
            self.user_grating[u['id']] = {
                'grating': 0.0,
                'login': u.get('login', ''),
                'winrate': u.get('winrate', 0.0),
                'lastNickname': u.get('lastNickname', ''),
                'mainIdx': u.get('mainIdx', '0')
            }

        temp_games = {}
        for gs in GAME_SESSION:
            if gs['win'] != '0' and gs['mode'] == '0':
                added_time = datetime.strptime(gs['added'], "%Y-%m-%d %H:%M:%S")
                temp_games[gs['id']] = {
                    'id': gs['id'],
                    'win': gs['win'],
                    'mode': gs['mode'],
                    'added': gs['added'],
                    'added_dt': added_time,  # Добавляем datetime версию
                    'players': [],
                    'time': 60  # Значение по умолчанию - максимальная длительность
                }

        for gsu in GAME_SESSION_USER:
            if gsu['gameId'] in temp_games:
                temp_games[gsu['gameId']]['players'].append({
                    'userId': gsu['userId'],
                    'team': gsu['team'],
                    'party': gsu['party'],
                    'afk': gsu['afk']
                })

        self.games = temp_games

        self._organize_games_by_users()

        self._identify_mains()

    def _identify_mains(self):
        for user_id in self.user_games:
            findMainAccount(user_id, self)

    def _organize_games_by_users(self):
        """Организуем игры по пользователям и создаем отсортированный список времен"""
        self.user_games = {}

        # Собираем все игры для каждого пользователя
        for game_id, game in self.games.items():
            for player in game['players']:
                user_id = player['userId']
                if user_id not in self.user_games:
                    self.user_games[user_id] = []
                self.user_games[user_id].append({
                    'game_id': game_id,
                    'time': game['added_dt']
                })

        # Сортируем игры по времени для каждого пользователя
        for user_id in self.user_games:
            self.user_games[user_id].sort(key=lambda x: x['time'])

        # Создаем отсортированный список всех времен игр для быстрого поиска
        self.sorted_game_times = sorted([game['added_dt'] for game in self.games.values()])

    def _calculate_game_durations(self):
        for game_id, game in self.games.items():
            current_time = game['added_dt']
            player_durations = []

            for player in game['players']:
                user_id = player['userId']
                next_game_time = self._find_next_game_time_fast(user_id, current_time)

                if next_game_time:
                    duration = (next_game_time - current_time).total_seconds() / 60  # в минутах
                    if duration > 0:  # Игнорируем отрицательные значения (ошибки в данных)
                        player_durations.append(duration)

            if player_durations:
                # Берем минимальную продолжительность среди всех игроков
                min_duration = min(player_durations)

                # Ограничиваем максимальную продолжительность 60 минутами
                game['time'] = min(min_duration, 61)
            else:
                # Если не нашли следующих игр, используем значение по умолчанию
                game['time'] = 61

    def _filter_games(self):
        print(f"Games before filtering {len(self.games.items())}")
        self.games = {key: value for key, value in self.games.items() if
                      (value.get('win') != '0') and
                      (value.get('win') != '3') and

                      (value.get('mode') != '4') and
                      (value.get('mode') != '5') and
                      (value.get('mode') != '97') and
                      (value.get('mode') != '98') and
                      (value.get('mode') != '99') and

                      (len(value.get('players', [])) > 2)
                      }
        print(f"Games after filtering {len(self.games.items())}")

    def _find_next_game_time(self, user_id, current_time):
        """Находит время следующей игры для пользователя после указанного времени"""
        if user_id not in self.user_games:
            return None

        user_games = self.user_games[user_id]

        # Ищем первую игру, которая начинается после current_time
        for game in user_games:
            if game['time'] > current_time:
                return game['time']

        return None

    def _find_next_game_time_fast(self, user_id, current_time):
        """Быстрый поиск следующей игры с использованием бинарного поиска"""
        if user_id not in self.user_games:
            return None

        user_games = self.user_games[user_id]

        # Используем бинарный поиск для нахождения позиции
        low, high = 0, len(user_games) - 1
        result = None

        while low <= high:
            mid = (low + high) // 2
            if user_games[mid]['time'] > current_time:
                result = user_games[mid]['time']
                high = mid - 1  # Продолжаем искать более раннюю игру
            else:
                low = mid + 1

        return result


def findMainAccount(userId, sim_data):
    usr = sim_data.user_grating[userId]
    if not findMain:
        if 'mainId' in usr and usr['mainId'] != '0':
            return usr['mainId']
        return userId

    if 'mainIdx' in usr and usr['mainIdx'] != '0':
        return usr['mainIdx']

    whiteMACAddress = ['00:50:56:c0:00:01', '00:50:56:c0:00:08', '88:88:88:88:87:88',
                       '0A:00:27:00:00:0B', '0A:00:27:00:00:0C', '0A:00:27:00:00:0D',
                       '0A:00:27:00:00:0E', '0A:00:27:00:00:0F']
    whiteMACAddress = [s.upper() for s in whiteMACAddress]


    macs = sim_data.users_2_macs.get(userId, [])
    filtered_macs = [item for item in macs if item.upper() not in whiteMACAddress]

    if len(filtered_macs) == 0:
        usr['mainIdx'] = userId
        return userId

    twinx = [userId]
    for mac in filtered_macs:
        if mac in sim_data.macs_2_users:
            twinx.extend(sim_data.macs_2_users[mac])
    twinx = sorted(list(set(twinx)))

    mainId = twinx[0]
    maxWr = int(sim_data.user_grating[twinx[0]].get('winrate', 0.0))
    for tw in twinx:
        if tw in sim_data.user_grating:
            wr = int(sim_data.user_grating[tw].get('winrate', 0.0))
            if wr > maxWr:
                mainId = tw
                maxWr = wr

    for tw in twinx:
        sim_data.user_grating[tw]['mainIdx'] = mainId

    #usr['mainIdx'] = mainId
    return mainId


def lerp(start, end, amount):
  return start + amount * (end - start)


def sigmoid(x):
    return 2.0 / (1.0 + math.exp(-x)) - 1


def fast_sigmoid(x):
    return sigmoid(x) if use_sigmoid else x / (1.0 + abs(x))


def prepareCombinations(teamSize):
    combinationsLocal = []
    original_range = list(range(teamSize * 2))
    combinations_out = list(itertools.combinations(original_range, teamSize))
    combinations_reversed = list(itertools.combinations(original_range[::-1], teamSize))
    combinationTargetLen = len(combinations_out) // 2
    for c, cr, i in zip(combinations_out, combinations_reversed, range(len(combinations_out))):
        combinations_out[i] = tuple(sorted(c))
        combinations_reversed[i] = tuple(sorted(cr))
    combinations_out.sort()
    combinations_reversed.sort(reverse=True)

    for c, cr, i in zip(combinations_out, combinations_reversed, range(len(combinations_out))):
        mc = sorted(list(c) + list(cr))
        if mc != original_range:
            raise Exception(f"Combination error! {i} {c} + {cr} = {mc} != {original_range}")

    for c in combinations_reversed:
        if len(combinations_out) <= combinationTargetLen:
            break
        combinations_out.remove(tuple(sorted(c)))
    for c in combinations_out:
        combinations_reversed.remove(tuple(sorted(c)))

    if len(combinations_out) != len(combinations_reversed):
        raise Exception("Invalid combinations")

    for i in range(len(combinations_out)):
        combinationsLocal.append(combinations_out[i])

    return combinationsLocal


def split_array_min_difference(ratings, parties, partySizes):
    if not party:
        return [], 0

    min_diff = float('inf')
    arr_sum = sum(ratings)
    bestSplit = None
    team1Diff = 0
    teamSize = len(ratings) // 2
    for comb in combinations[teamSize]:
        partyCounter = {}
        sum1 = 0
        for p in range(teamSize):
            playerIdx = comb[p]
            sum1 += ratings[playerIdx]
            playerParty = parties[playerIdx]
            if playerParty != 0:
                partyCounter[playerParty] = partyCounter.get(playerParty, 0) + 1

        allValuesMatch = all(partySizes[key] == partyCounter[key] for key in partyCounter)
        if not allValuesMatch:
            continue

        sum2 = arr_sum - sum1
        diff = abs(sum1 - sum2)

        if diff < min_diff:
            min_diff = diff
            bestSplit = comb
            team1Diff = sum1 - sum2
    return bestSplit, min_diff, team1Diff


def formula(playerRating, baseRatingGain, teamDiff, softCap):
    fsPlayer = fast_sigmoid(playerRating)
    fsSoft = fast_sigmoid(softCap)
    fsTeamDiff = fast_sigmoid(teamDiff)
    win = (1.0 - fsPlayer) * (0.5 - fsSoft) * baseRatingGain * (1.0 - fsTeamDiff)
    loss = -(1.0 + fsPlayer) * (0.5 + fsSoft) * baseRatingGain * (1.0 + fsTeamDiff)
    return [win, loss]


def calcRating(sim_data, game, params, i):
    players = game['players']
    win = game['win']

    teams = {'1': [], '2': []}
    teams_rating = {'1': 0, '2': 0}

    possibleTD_Players = []
    possibleTD_Parties = []
    partySizesTD = {}

    # Count party sizes and per player party
    for p in players:
        playerParty = int(p['party'])

        if playerParty != 0:
            if playerParty not in partySizesTD:
                partySizesTD[playerParty] = 0
            partySizesTD[playerParty] += 1

    if len(partySizesTD) != 0:
        sim_data.games_with_parties[game['id']] = len(partySizesTD)

    # Count team ratings
    for p in players:
        playerParty = int(p['party'])
        playerTeam = p['team']

        teams[playerTeam].append(p)
        main_account = findMainAccount(p['userId'], sim_data)
        player_data = sim_data.user_grating[main_account]
        player_rating = player_data['grating']

        teams_rating[playerTeam] += player_rating
        possibleTD_Players.append(player_rating)
        possibleTD_Parties.append(playerParty)

    possibleTD_Split, possibleTD_Rating, possibleT1Diff = split_array_min_difference(possibleTD_Players, possibleTD_Parties, partySizesTD)

    teams_rating_diff = {'1': teams_rating['1'] - teams_rating['2']}
    teams_rating_diff['2'] = -teams_rating_diff['1']

    # Calculate target optimization metrics and graph data
    if i == 0 or i == SIM_STEPS - 1:
        cs = 1 if i == SIM_STEPS - 1 else 0

        sim_data.outcomes[cs].append(1 if win == '1' else 0)

        sim_data.original_diffs[cs].append(teams_rating_diff['1'])
        sim_data.possible_diffs[cs].append(possibleTD_Rating)

        predicted_win = '1' if teams_rating_diff['1'] > 0 else '2'
        predicted_outcome = 1 if predicted_win == win else 0
        sim_data.original_predictions[cs].append(predicted_outcome)

        added = matplotlib.dates.date2num(datetime.strptime(game['added'][:10], "%Y-%m-%d"))
        sim_data.added_games[cs].append(added)

    team1WinProbability = 1 - formula(0, 1, possibleT1Diff * TEAM_RATING_MOD, 0)[0]
    possibleWin = '1' if random.random() > team1WinProbability else '2'
    # Calculate new player rating
    for p in players:
        main_account = findMainAccount(p['userId'], sim_data)
        player = sim_data.user_grating[main_account]

        player_division_rating = math.fmod(abs(player['grating']), RATING_BUCKET * 2)
        if player['grating'] < 0:
            player_division_rating = RATING_BUCKET * 2 - player_division_rating

        new_games = (not descent) and (i != 0) and (int(game['id']) > games_with_team_ratings) and False
        if new_games:
            team_diff_rating = possibleT1Diff if p['team'] == '1' else -possibleT1Diff
            win = possibleWin
        else:
            team_diff_rating = teams_rating_diff[p['team']]

        grating_mod_normalized = (player_division_rating - RATING_BUCKET) / RATING_BUCKET
        soft_cap_value = (player['grating'] - params.get('BOTTOM_RATING', BOTTOM_RATING)) / (params.get('TOP_RATING', TOP_RATING) - params.get('BOTTOM_RATING', BOTTOM_RATING)) - 0.5
        win_loss = formula(abs(params.get('RATING_POWER', RATING_POWER)) * grating_mod_normalized,
                           abs(params.get('RATING_GAIN', RATING_GAIN)) + MIN_RATING_GAIN,
                           abs(params.get('TEAM_RATING_MOD', TEAM_RATING_MOD)) * team_diff_rating,
                           soft_cap_value * abs(params.get('SOFT_RATING_POWER', SOFT_RATING_POWER)))
        if i == 0 or change_sim1_rating:
            if p['team'] == win and p['afk'] != '1':
                player['grating'] += win_loss[0]
            else:
                player['grating'] += win_loss[1]
        player['grating'] = round(player['grating'], 2)


def calcMeanWinrate(original_diffs, median_diff, predictions):
    for m in range(len(original_diffs)):
        original_diffs[m] = abs(original_diffs[m] - median_diff)

    diff_len = math.ceil(len(original_diffs) // (100 / SANITY_PERCENTILE))
    sorted_outcomes = sorted(zip(original_diffs, predictions), key=lambda x: x[0])[:diff_len]
    return sum(1 for item in sorted_outcomes if item[1] == 1) / diff_len


def writeSimulationResult(sim_data):
    user_ratings = [{'id': int(key), 'rating': sim_data.user_grating[key]['grating']} for key in sim_data.user_grating]
    with open('user_ratings.json', 'w', encoding='utf-8') as rf:
        json.dump(user_ratings, rf)


def simulate_process(params, sim_data):
    for i in range(SIM_STEPS):
        for game_id, game in sim_data.games.items():
            try:
                players = game.get('players', [])
                if players:
                    first_player = players[0]
                    player_user_id = first_player.get('userId')

                    main_account = findMainAccount(player_user_id, sim_data)
                    if main_account not in sim_data.user_grating:
                        continue

                calcRating(sim_data, game, params, i)

            except Exception as e:
                print(f"   ❌ Error in game {game_id}: {e}")
                traceback.print_exc()
                continue

        if i == 0 or i == SIM_STEPS - 1:
            if i == 0:
                writeSimulationResult(sim_data)
            cs = 1 if i == SIM_STEPS - 1 else 0
            partyOutcomes = [y for y, x in zip(sim_data.outcomes[cs], list(range(len(sim_data.outcomes[cs])))) if str(x) in sim_data.games_with_parties]
            partyDiffs = [y for y, x in zip(sim_data.original_diffs[cs], list(range(len(sim_data.original_diffs[cs])))) if str(x) in sim_data.games_with_parties]

            #auc_wo = roc_auc_score(sim_data.outcomes[cs], sim_data.original_diffs[cs])
            auc_wo = roc_auc_score(partyOutcomes, partyDiffs)

            possible_sanity_abs_mean = abs(numpy.mean(sim_data.possible_diffs[cs]))
            possible_sanity_std = numpy.std(sim_data.possible_diffs[cs])

            total_original_wr = calcMeanWinrate(sim_data.original_diffs[cs].copy(), statistics.median(numpy.abs(sim_data.original_diffs[cs])),
                                                sim_data.original_predictions[cs])

            total_possible_wr = calcMeanWinrate(sim_data.original_diffs[cs].copy(), statistics.median(numpy.abs(sim_data.possible_diffs[cs])),
                                                sim_data.original_predictions[cs])

            sanity = 0
            sanity += (LAMBDA_AUC * auc_wo)
            sanity += (LAMBDA_NEW_MEAN / (1.0 + possible_sanity_abs_mean))
            sanity += (LAMBDA_NEW_STD / (1.0 + possible_sanity_std))
            sanity += (LAMBDA_ORIG_WR * abs(total_original_wr - 0.5))
            sanity += (LAMBDA_NEW_WR * (1.0 / (1.0 + abs(total_possible_wr - 0.5))))

            lambda_sum = LAMBDA_AUC + LAMBDA_NEW_MEAN + LAMBDA_NEW_STD + LAMBDA_ORIG_WR + LAMBDA_NEW_WR
            sanity /= lambda_sum

            sim_data.sanity.append(sanity)
            sim_data.auc_wo.append(auc_wo)
            sim_data.possible_sanity_abs_mean.append(possible_sanity_abs_mean)
            sim_data.possible_sanity_std.append(possible_sanity_std)
            sim_data.original_outcome_wr.append(total_original_wr)
            sim_data.possible_outcome_wr.append(total_possible_wr)

            def prepareWRData(diffs, target):
                tmp_diff_data = []
                percentiles = []
                percentile_step = 100 / diff_buckets
                for bucket in range(1, diff_buckets + 1):
                    diffPercentile = numpy.percentile(diffs, bucket * percentile_step)
                    percentiles.append(diffPercentile)
                    diffWR = calcMeanWinrate(sim_data.original_diffs[cs].copy(), diffPercentile,
                                             sim_data.original_predictions[cs])
                    tmp_diff_data.append(diffWR)

                for teamDiff in diffs:
                    bucket_idx_end = 0
                    for pd in range(1, diff_buckets):
                        if teamDiff <= percentiles[pd]:
                            bucket_idx_end = pd
                            break

                    bucket_idx_end = min(bucket_idx_end, diff_buckets - 1)
                    bucket_idx_start = bucket_idx_end - 1
                    percentile_start = 0 if bucket_idx_start < 0 else percentiles[bucket_idx_start]
                    percentile_end = percentiles[bucket_idx_end]
                    bucket_lerp = (teamDiff - percentile_start) / (percentile_end - percentile_start)
                    bucket_value = lerp(tmp_diff_data[bucket_idx_start], tmp_diff_data[bucket_idx_end], bucket_lerp)
                    target.append(bucket_value)

            if not descent:
                prepareWRData(numpy.abs(sim_data.original_diffs[cs]), sim_data.original_wrs[cs])
                prepareWRData(numpy.abs(sim_data.possible_diffs[cs]), sim_data.possible_wrs[cs])


                for o, p in zip(sim_data.original_diffs[cs], sim_data.possible_diffs[cs]):
                    sim_data.original_team_diff_mod[cs].append(1 - formula(0, 1, abs(o) * TEAM_RATING_MOD, 0)[0])
                    sim_data.possible_team_diff_mod[cs].append(1 - formula(0, 1, abs(p) * TEAM_RATING_MOD, 0)[0])
                player_ratings = []
                for p in sim_data.user_grating:
                    player_rating = sim_data.user_grating[p]['grating']
                    if player_rating != 0:
                        player_ratings.append(sim_data.user_grating[p]['grating'] * DEFAULT_RATING_MULT + DEFAULT_RATING)
                sim_data.player_ratings.append(player_ratings)


    return sim_data


def create_shared_data():
    sim_data = SimulationData()
    sim_data.init()

    shared_data = {
        'users_2_macs': sim_data.users_2_macs,
        'macs_2_users': sim_data.macs_2_users,
        'games': sim_data.games,
        'user_grating': sim_data.user_grating
    }

    return shared_data


def create_local_sim_data(shared_data):
    local_sim_data = SimulationData()

    local_sim_data.users_2_macs = shared_data['users_2_macs']
    local_sim_data.macs_2_users = shared_data['macs_2_users']
    local_sim_data.games = shared_data['games']
    local_sim_data.user_grating = shared_data['user_grating']

    return local_sim_data


def simulate_wrapper(args):
    epochIJ, params, shared_data = args
    print(f"{epochIJ} = {params}")

    for i in range(TEAM_SIZE):
        combinations[i + 1] = prepareCombinations(i + 1)
    try:
        if profile_enabled:
            profiler.start()
        local_sim_data = create_local_sim_data(shared_data)

        result = simulate_process(params, local_sim_data)
        if profile_enabled:
            profiler.stop()
            print(profiler.output_text(unicode=True, color=True))

        return epochIJ, result

    except Exception as e:
        print(f"❌ ERROR in process {epochIJ}: {e}")

        exc_type, exc_value, exc_traceback = sys.exc_info()
        tb_lines = traceback.format_exception(exc_type, exc_value, exc_traceback)
        tb_text = ''.join(tb_lines)

        print(f"📝 Full traceback for epoch {epochIJ}:")
        print(tb_text)

        return epochIJ, None


def run_gradient_descent(shared_data):
    if not descent:
        return False
    grad_tasks = []
    paramN = len(global_params)

    for step in range(0, STEPS * 2):
        actual_step = step - STEPS + (step >= STEPS)
        for paramId in range(paramN):
            newParams = {}
            for gp in global_params:
                newParams[gp] = global_params[gp][0]

            step_sign = -1 if actual_step < 0 else 1
            newParams[list(global_params)[paramId]] += (step_sign * math.pow(0.5, (abs(actual_step) - 1)) * global_params[list(global_params)[paramId]][1])

            grad_tasks.append((step * paramN + paramId, newParams, shared_data))

    oldParams = {}
    for gp in global_params:
        oldParams[gp] = global_params[gp][0]
    grad_tasks.append((STEPS * 2 * paramN, oldParams, shared_data))

    print(f"Starting {len(grad_tasks)} simulations using ProcessPoolExecutor...")
    results = {}
    with ProcessPoolExecutor(max_workers=multiprocessing.cpu_count()) as executor:
        future_to_epochIJ = {executor.submit(simulate_wrapper, task): task[0] for task in grad_tasks}

        for future in as_completed(future_to_epochIJ):
            epochIJ = future_to_epochIJ[future]
            try:
                epochIJ_result, result_data = future.result()
                results[epochIJ] = result_data
            except Exception as e:
                print(f"Epoch {epochIJ} generated an exception: {e}")

    sanity = zeros((paramN, STEPS * 2))
    current_sanity = 0

    for epochIJ, result in results.items():
        sanity_ = result.sanity
        sanity_initial_ = sanity_[0]
        sanity_final_ = sanity_[1]
        sanity_avg = math.sqrt(sanity_initial_ * sanity_final_)
        step = epochIJ // paramN
        paramId = epochIJ % paramN
        print(f"epochIJ = {epochIJ}; step = {step}; paramId = {paramId}; "
              f"si = {sanity_initial_}; "
              f"sf = {sanity_final_}; "
              f"sa = {sanity_avg}")
        if epochIJ == STEPS * 2 * paramN:
            current_sanity = sanity_avg

            print(f"Current sanity = {result.sanity}; "
                  f"original_outcome_wr = {result.original_outcome_wr}; "
                  f"possible_outcome_wr = {result.possible_outcome_wr}; "
                  f"auc = {result.auc_wo}; \n"
                  f"mean = {result.possible_sanity_abs_mean}; "
                  f"std = {result.possible_sanity_std}; "
                  )
            continue

        sanity[paramId, step] = sanity_avg

    best_param_step = zeros(paramN)
    best_param_sanity_comparison = zeros(paramN)

    best_param_sanity = zeros(paramN)
    best_param_sanity_normalized = zeros(paramN)

    for paramId in range(paramN):
        best_param_step[paramId] = -1
        best_param_sanity_comparison[paramId] = current_sanity

    for paramId in range(paramN):
        for step in range(0, STEPS * 2):
            cur_best = best_param_sanity_comparison[paramId]
            pot_best = sanity[paramId, step]
            if pot_best > cur_best:
                best_param_step[paramId] = step
                best_param_sanity_comparison[paramId] = pot_best

                best_param_sanity[paramId] = pot_best
                best_param_sanity_normalized[paramId] = pot_best - current_sanity

    if max(best_param_sanity) == 0:
        print(f"Worse results! Was {current_sanity}")
        print(global_params)
        return False

    minBestParam = min(x for x in best_param_sanity_normalized if x != 0)
    maxBestParam = max(x for x in best_param_sanity_normalized if x != 0)
    for paramId in range(paramN):
        #best_param_sanity_normalized[paramId] = best_param_sanity_normalized[paramId] == maxBestParam
        best_param_sanity_normalized[paramId] /= maxBestParam
        best_param_sanity_normalized[paramId] = math.pow(best_param_sanity_normalized[paramId], 2)
    best_sanity_magnitude = numpy.linalg.norm(best_param_sanity_normalized)
    best_param_sanity_normalized = best_param_sanity_normalized / best_sanity_magnitude

    print(f"New best! Was {current_sanity}, now {best_param_sanity}")
    print(f"Normalized sanity {best_param_sanity_normalized}")
    print(f"Best steps {best_param_step}")
    print(f"Current params {global_params}")
    for paramId in range(paramN):
        actual_step = best_param_step[paramId] - STEPS + (best_param_step[paramId] >= STEPS)
        if best_param_step[paramId] == -1:
            actual_step = 0

        step_sign = -1 if actual_step < 0 else 1
        global_params[list(global_params)[paramId]][0] += (
                global_params[list(global_params)[paramId]][1] * best_param_sanity_normalized[paramId] *
                step_sign * math.pow(0.5, (abs(actual_step) - 1))
        )

    print(f"New params {global_params}")
    return True


def main():
    print(f"Available CPU cores: {multiprocessing.cpu_count()}")

    shared_data = create_shared_data()
    while run_gradient_descent(shared_data):
        continue

    global descent
    descent = False
    tasks = []
    for j in range(EPOCHS):
        for k in range(EPOCHS):
            newParams = {}
            for gp in global_params:
                newParams[gp] = global_params[gp][0] + global_params[gp][1] * (
                        j * (optimizing_params[0] == gp) +
                        k * (optimizing_params[0] == gp)
                )

            epochIJ = j * EPOCHS + k

            tasks.append((epochIJ, newParams, shared_data))

    print(f"Starting {len(tasks)} simulations using ProcessPoolExecutor...")
    start_time = time.time()

    results = {}
    with ProcessPoolExecutor(max_workers=multiprocessing.cpu_count()) as executor:
        future_to_epochIJ = {executor.submit(simulate_wrapper, task): task[0] for task in tasks}

        for future in as_completed(future_to_epochIJ):
            epochIJ = future_to_epochIJ[future]
            try:
                epochIJ_result, result_data = future.result()
                results[epochIJ] = result_data
            except Exception as e:
                print(f"Epoch {epochIJ} generated an exception: {e}")

    end_time = time.time()
    print(f"All simulations completed in {end_time - start_time:.2f} seconds")

    process_results(results)


def pprint(idd, simData):
    curUser = str(idd)
    mainUser = findMainAccount(str(idd), simData)
    userGr = DEFAULT_RATING + simData.user_grating[mainUser]['grating']

    print (f"{simData.user_grating[curUser]['login']}[ {curUser} ] aka {simData.user_grating[mainUser]['login']} [ {mainUser} ] ( {simData.user_grating[mainUser]['lastNickname']} ) = {userGr}")


def process_results(results):
    print("Processing results...")

    for epochIJ, result in results.items():
        if result is None:
            continue

        j = epochIJ // EPOCHS
        k = epochIJ % EPOCHS

        print(f"Epoch {j} / {k} "
              f" = {result.sanity}; "
              f"original_outcome_wr = {result.original_outcome_wr}; "
              f"possible_outcome_wr = {result.possible_outcome_wr}; "
              f"auc = {result.auc_wo}; \n"
              f"mean = {result.possible_sanity_abs_mean}; "
              f"std = {result.possible_sanity_std}; "
              )

        pprint(1, result)
        pprint(2, result)
        pprint(24, result)
        pprint(131, result)
        pprint(134, result)
        pprint(865, result)
        pprint(10080, result)
        pprint(1016, result)
        pprint(2295, result)

    plot_results(results)


fig, axs = plt.subplots(2, 2, figsize=(12, 8))
def plot_results(results):
    print("Plotting results...")
    final_suffix = f' после {SIM_STEPS} повторов всех боёв'

    def plot_distribution(axis, title, items):
        axis.set_title(title)
        axis.hist(items, bins=50, edgecolor='black', color='skyblue')

    def plot_heatmap(axis, xAxis_array, yAxis_array, title, isTime = True, borders = False):
        if borders:
            xAxis_array.append(0.4)
            xAxis_array.append(1)
            yAxis_array.append(0.4)
            yAxis_array.append(1)
        heatmap, xEdges, yEdges = numpy.histogram2d(xAxis_array, yAxis_array, bins=(len(set(xAxis_array)), diff_buckets))
        norms = numpy.linalg.norm(heatmap, axis=1, keepdims=True, ord=1)
        heatmap = heatmap / numpy.clip(norms, 0.001, float('inf'))
        axis.set_title(title)
        axis.imshow(heatmap.T, cmap='BuGn', aspect='auto', interpolation='nearest', extent=[xEdges[0], xEdges[-1], yEdges[-1], yEdges[0]])
        if isTime:
            axis.xaxis.set_major_locator(matplotlib.dates.AutoDateLocator())
            axis.xaxis.set_major_formatter(matplotlib.dates.DateFormatter('%Y-%m-%d'))
            axis.tick_params(axis='x', rotation=45)
        axis.invert_yaxis()

    for epochIJ, result in results.items():
        if epochIJ == 0:
            new_added_games = []
            new_original_wrs0 = []
            new_original_wrs1 = []
            new_possible_wrs0 = []
            new_possible_wrs1 = []
            gameItems = list(result.games.items())
            for gId in range(len(gameItems)):
                game_id, game = gameItems[gId]
                if game_id in result.games_with_parties:
                    new_added_games.append(result.added_games[0][gId])
                    new_original_wrs0.append(result.original_wrs[0][gId])
                    new_original_wrs1.append(result.original_wrs[1][gId])
                    new_possible_wrs0.append(result.possible_wrs[0][gId])
                    new_possible_wrs1.append(result.possible_wrs[1][gId])

            plot_heatmap(axs[0, 0], result.added_games[0], result.original_wrs[0],
                              f'Баланс команд до патча')
            #plot_heatmap(axs[1, 0], result.added_games[0], result.original_wrs[1],
            #                  f'Баланс команд' + final_suffix)
            plot_heatmap(axs[0, 1], result.added_games[0], result.possible_wrs[0],
                              f'Потенциально возможный баланс команд до патча')
            #plot_heatmap(axs[0, 2], result.added_games[0], result.possible_wrs[0],
            #                  f'Потенциально возможный баланс команд в будущем')
            plot_distribution(axs[1, 0], f'Распределение рейтингов', result.player_ratings[0])
            #plot_distribution(axs[1, 1], f'Распределение рейтингов' + final_suffix, result.player_ratings[1])
            #plot_heatmap(axs[2, 0], new_added_games, numpy.abs(new_original_wrs0),
            #                  f'Original team with party balance timeline')
            #plot_heatmap(axs[3, 0], new_added_games, numpy.abs(new_original_wrs1),
            #                  f'Original team with party balance timeline' + final_suffix)
            #plot_heatmap(axs[2, 1], new_added_games, numpy.abs(new_possible_wrs0),
            #                  f'Possible team with party balance timeline')
            #plot_heatmap(axs[3, 1], new_added_games, numpy.abs(new_possible_wrs1),
            #                  f'Possible team with party balance timeline' + final_suffix)
            #plot_heatmap(axs[2, 2], result.original_team_diff_mod[0].copy(), result.original_wrs[0].copy(),
            #                  f'Original team diff winrate', False, True)
            #axs[2, 2].plot([0, 1], [0, 1], color='green', alpha=0.5 , linestyle='--', linewidth=1, transform=axs[2, 2].transAxes)
            plot_heatmap(axs[1, 1], result.possible_team_diff_mod[0].copy(), result.possible_wrs[0].copy(),
                              f'Зависимость винрейта сильной команды от нормализованной разницы рейтингов', False, True)
            axs[1, 1].plot([0, 1], [0, 1], color='red', alpha=0.5 , linestyle='--', linewidth=1, transform=axs[1, 1].transAxes)

    plt.tight_layout()
    plt.show()


if __name__ == "__main__":
    multiprocessing.freeze_support()
    main()